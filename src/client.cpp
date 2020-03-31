#include <mutex>
#include <string>
#include <utility>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <climits>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "nlohmann/json.hpp"

#include "ce2103/hash.hpp"
#include "ce2103/network.hpp"

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/client.hpp"
#include "ce2103/mm/session.hpp"

using nlohmann::json;

namespace
{
	std::optional<ce2103::mm::remote_manager> remote_collector;
}

namespace ce2103::mm
{
	client_session::client_session(socket client_socket, std::string_view secret)
	: session{std::move(client_socket)}
	{
		std::string hash_output;
		hash_output.reserve(sizeof(std::uint64_t[2]) * 2);

		auto push_bytes = [&hash_output](std::uint64_t half)
		{
			for(int i = sizeof half - 1; i >= 0; --i)
			{
				hash_output.push_back(static_cast<char>(half >> (i * CHAR_BIT)));
			}
		};

		auto hash = md5::of(secret);
		push_bytes(hash.first);
		push_bytes(hash.second);

		serialize_octets(hash_output);

		this->send({{"op", "auth"}, {"hash", std::move(hash_output)}});
		if(this->receive() != json{{"value", true}})
		{
			this->discard();
		}
	}

	bool client_session::finalize()
	{
		this->send({{"op", "bye"}});

		bool cleanly_finalized = this->receive() == json({});
		this->discard();

		return cleanly_finalized;
	}

	std::optional<std::size_t> client_session::allocate(std::size_t size)
	{
		std::lock_guard lock{this->mutex};

		this->send({{"op", "alloc"}, {"size", size}});
		return this->expect_value<std::size_t>();
	}

	std::optional<std::size_t> client_session::lift(std::size_t id)
	{
		return this->do_id_operation<std::size_t>("lift", id);
	}

	std::optional<std::size_t> client_session::drop(std::size_t id)
	{
		return this->do_id_operation<std::size_t>("drop", id);
	}

	std::optional<std::string> client_session::fetch(std::size_t id)
	{
		auto result = this->do_id_operation<std::string>("read", id);
		if(!result || !deserialize_octets(*result, result->length() / 2))
		{
			result = std::nullopt;
		}

		return result;
	}

	bool client_session::overwrite(std::size_t id, std::string contents)
	{
		std::lock_guard lock{this->mutex};

		serialize_octets(contents);
		this->send({{"op", "write"}, {"id", id}, {"value", std::move(contents)}});

		bool succeeded = this->receive() == json({});
		if(!succeeded)
		{
			this->discard();
		}

		return succeeded;
	}

	template<typename T>
	std::optional<T> client_session::do_id_operation(const char* operation, std::size_t id)
	{
		std::lock_guard lock{this->mutex};

		this->send({{"op", operation}, {"id", id}});
		return this->expect_value<T>();
	}

	template<typename T>
	std::optional<T> client_session::expect_value()
	{
		try
		{
			if(auto result = this->receive())
			{
				return static_cast<T>(result->at("value"));
			}
		} catch(const json::exception&)
		{
			// Fall-through
		}

		this->discard();
		return std::nullopt;
	}

	bool remote_manager::initialize(socket client_socket, std::string_view secret)
	{
		assert(!remote_collector);

		return !remote_collector.emplace
		(
			private_t{}, std::move(client_socket), secret
		).client.is_lost();
	}

	remote_manager& remote_manager::get_instance() noexcept
	{
		assert(remote_collector);
		return *remote_collector;
	}

	remote_manager::remote_manager(private_t, socket client_socket, std::string_view secret)
	: client{std::move(client_socket), secret}
	{
		this->install_trap_region();
	}

	std::size_t remote_manager::lift(std::size_t id)
	{
		auto count = this->client.lift(id);
		if(!count)
		{
			throw_network_failure();
		}

		return *count - 1;
	}

	std::size_t remote_manager::drop(std::size_t id)
	{
		auto count = this->client.drop(id);
		if(!count)
		{
			throw_network_failure();
		} else if(*count > 1)
		{
			return *count - 1;
		}

		dispose(*static_cast<allocation*>(this->allocation_base_for(id)));

		//! There might be a pending writeback operation
		this->evict(id);

		if(!this->client.drop(id))
		{
			throw_network_failure();
		}

		return 0;
	}

	[[noreturn]]
	void remote_manager::throw_network_failure()
	{
		throw std::runtime_error{"Network failure"};
	}

	std::pair<std::size_t, void*> remote_manager::allocate(std::size_t size)
	{
		auto id = this->client.allocate(size);
		if(!id || !this->client.lift(*id))
		{
			throw_network_failure();
		}

		this->wipe(*id, size);
		return std::make_pair(*id, this->allocation_base_for(*id));
	}
}

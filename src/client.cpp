#include <mutex>
#include <string>
#include <utility>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <climits>
#include <optional>
#include <string_view>
#include <system_error>

#include "nlohmann/json.hpp"

#include "ce2103/hash.hpp"
#include "ce2103/network.hpp"

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/error.hpp"
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
		std::uint8_t hash_bytes[sizeof(std::uint64_t[2])];
		auto put_half = [&hash_bytes](std::size_t at, std::uint64_t half) noexcept
		{
			for(std::size_t i = 0; i < sizeof half; ++i)
			{
				hash_bytes[at + i] = static_cast<std::uint8_t>
				(
					half >> ((sizeof half - i - 1) * CHAR_BIT)
				);
			}
		};

		auto hash = md5::of(secret);
		put_half(0, hash.first);
		put_half(sizeof(std::uint64_t), hash.second);

		auto view = std::string_view{reinterpret_cast<char*>(hash_bytes), sizeof hash_bytes};
		this->send({{"op", "auth"}, {"hash", serialize_octets(view)}});

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
		if(auto serialized = this->do_id_operation<nlohmann::json>("read", id))
		{
			if(auto size = deserialized_size(*serialized))
			{
				std::string contents;
				contents.resize(*size);

				if(deserialize_octets(*serialized, contents.data(), *size))
				{
					return contents;
				}
			}
		}

		return std::nullopt;
	}

	bool client_session::overwrite(std::size_t id, std::string_view contents)
	{
		std::lock_guard lock{this->mutex};

		this->send({{"op", "write"}, {"id", id}, {"value", serialize_octets(contents)}});

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
		throw std::system_error{error_code::network_failure};
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

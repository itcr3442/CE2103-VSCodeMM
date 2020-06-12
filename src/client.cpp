#include <mutex>
#include <string>
#include <utility>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <climits>
#include <optional>
#include <typeinfo>
#include <algorithm>
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
	//! Singleton instance
	std::optional<ce2103::mm::remote_manager> remote_collector;
}

namespace ce2103::mm
{
	client_session::client_session(socket client_socket, std::string_view secret)
	: session{std::move(client_socket)}
	{
		// Transforms the array of uint64_ts into a standard MD5 representation
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
		this->send({{"auth", serialize_octets(view)}});

		if(this->receive() != json(true))
		{
			this->discard();
		}
	}

	bool client_session::finalize()
	{
		this->send({{"bye", {}}});

		bool cleanly_finalized = this->receive() == json({});
		this->discard();

		return cleanly_finalized;
	}

	std::optional<std::size_t> client_session::allocate
	(
		std::size_t part_size, std::size_t parts, std::size_t remainder, const char* type
	)
	{
		std::lock_guard lock{this->mutex};

		// The first part will start with a refcount of 2
		json query{{"alloc", 1}, {"type", type}};
		if(remainder > 0)
		{
			query["rem"] = remainder;
		}

		if(part_size > 0 && parts > 0)
		{
			query["unit"] = part_size;
			query["parts"] = parts;
		}

		this->send(std::move(query));

		auto result = this->receive();
		if(!result || !result->is_number_unsigned())
		{
			this->discard();
			return std::nullopt;
		}

		return result->get<std::size_t>();
	}

	bool client_session::lift(std::size_t id)
	{
		this->send({{"lift", id}});
		return this->expect_empty();
	}

	std::optional<drop_result> client_session::drop(std::size_t id)
	{
		this->send({{"drop", id}});

		auto result = this->receive();
		if(result == json({}))
		{
			return drop_result::reduced;
		} else if(result == json{{"hanging", true}})
		{
			return drop_result::hanging;
		} else if(result == json{{"lost", true}})
		{
			return drop_result::lost;
		} else
		{
			this->discard();
			return std::nullopt;
		}
	}

	std::optional<std::string> client_session::fetch(std::size_t id)
	{
		this->send({{"read", id}});
		if(auto serialized = this->receive())
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

		this->send({{"write", id}, {"value", serialize_octets(contents)}});
		return this->expect_empty();
	}

	bool client_session::expect_empty()
	{
		bool succeeded = this->receive() == json({});
		if(!succeeded)
		{
			this->discard();
		}

		return succeeded;
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

		bool succeeded = !remote_collector.emplace
		(
			private_t{}, std::move(client_socket), secret
		).client.is_lost();

		if(!succeeded)
		{
			remote_collector.reset();
		}

		return succeeded;
	}

	remote_manager& remote_manager::get_instance()
	{
		if(!remote_collector)
		{
			throw std::system_error{error_code::no_remote_session};
		}

		return *remote_collector;
	}

	remote_manager::remote_manager(private_t, socket client_socket, std::string_view secret)
	: client{std::move(client_socket), secret}
	{
		this->install_trap_region();
	}

	[[noreturn]]
	void remote_manager::throw_network_failure()
	{
		throw std::system_error{error_code::network_failure};
	}

	std::size_t remote_manager::allocate(std::size_t size, const std::type_info& type)
	{
		std::size_t part_size = this->get_part_size();

		// Divides the requested size by parts; eg, 9000 becomes 4096 + 4096 + 808
		auto id = this->client.allocate
		(
			part_size, size / part_size, size % part_size, type.name()
		);

		if(!id)
		{
			throw_network_failure();
		}

		// Optimizes writing of the allocation header in the near future
		this->wipe(*id, std::min(size, part_size));
		return *id;
	}

	void remote_manager::do_lift(std::size_t id)
	{
		if(!this->client.lift(id))
		{
			throw_network_failure();
		}
	}

	drop_result remote_manager::do_drop(std::size_t id)
	{
		auto result = this->client.drop(id);
		if(!result)
		{
			throw_network_failure();
		}

		switch(*result)
		{
			case drop_result::reduced:
				return drop_result::reduced;

			case drop_result::hanging:
			{
				allocation& header = this->get_base_of(id);
				this->probe(&header, true);

				std::size_t total_size = header.get_total_size();
				std::size_t parts = (total_size - 1) / this->get_part_size() + 1;

				// This destroys all objects in the allocation
				dispose(header);

				// Finally, free all of the allocation's parts
				for(std::size_t part = id; part < id + parts; ++part)
				{
					//! There might be a pending writeback operation
					this->evict(part);

					if(!this->client.drop(part))
					{
						throw_network_failure();
					}
				}

				return drop_result::lost;
			}

			default:
				// Unreachable
				throw_network_failure();
		}
	}
}

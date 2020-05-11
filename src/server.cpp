#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <algorithm>
#include <functional>
#include <string_view>

#include "ce2103/hash.hpp"
#include "ce2103/list.hpp"
#include "ce2103/network.hpp"
#include "ce2103/hash_map.hpp"

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/init.hpp"
#include "ce2103/mm/session.hpp"

using secret_hash = decltype(ce2103::md5::of({}));
using nlohmann::json;

using ce2103::mm::garbage_collector;
using ce2103::mm::drop_result;

namespace
{
	//! Server-side representation of a session.
	class server_session : public ce2103::mm::session
	{
		public:
			//! Constructs a new session given a client socket and secret hash.
			inline server_session(ce2103::socket client, const secret_hash& secret)
			: ce2103::mm::session{std::move(client)}, secret{secret}
			{}

			//! Move-constructs a new session
			server_session(server_session&& other) = default;

			//! Destroys the session
			~server_session();

			//! Replaces the session with another one
			server_session& operator=(server_session&& other) = default;

			//! Reads a command and processes it.
			bool on_input();

		private:
			//! MD5 hash of the preshared key
			std::reference_wrapper<const secret_hash> secret;

			//! ID-to-(base, size) map of active objects for this session.
			ce2103::hash_map<std::size_t, std::pair<char*, std::size_t>> objects;

			//! Whether the client has been authorized
			bool authorized = false;

			//! Attempts to authorize the client with the given PSK hash.
			void authorize(const nlohmann::json& input);

			//! Finalizes the session.
			void finalize();

			//! Allocates a new region of memory.
			void allocate
			(
				std::size_t part_size, std::size_t parts,
				std::size_t remainder, std::size_t initial_count
			);

			//! Incremnts an object's reference count.
			void lift(std::size_t id);

			//! Decrements an object's reference count.
			void drop(std::size_t id);

			//! Dumps the given object's memory contents to the client.
			void read_contents(std::size_t id);

			//! Overwrites an object's memory contents.
			void write_contents(std::size_t id, const nlohmann::json& contents);

			//! Retrieves an active pair (see "objects"), otherwise reports client failure.
			std::pair<char*, std::size_t>* expect_extant(std::size_t id) noexcept;

			//! Sends an empty response ("{}")
			void send_empty();

			//! Fails with the given error message.
			void send_error(const char* message);

			//! Indicates an invalid client request.
			void fail_bad_request();

			//! Indicates an incorrect size in the client request.
			void fail_wrong_size();
	};

	server_session::~server_session() noexcept
	{
		for(const auto& [id, pair] : this->objects)
		{
			while(garbage_collector::get_instance().drop(id) != drop_result::lost)
			{
				continue;
			}
		}
	}

	bool server_session::on_input()
	{
		auto command = this->receive();
		if(!command)
		{
			this->fail_bad_request();
			this->discard();

			return false;
		}

		try
		{
			if(auto hash = command->find("auth"); hash != command->end())
			{
				this->authorize(*hash);
			} else if(command->contains("bye"))
			{
				this->finalize();
			} else if(!this->authorized)
			{
				this->send_error("unauthorized");
			} else if(auto lifts = command->find("alloc"); lifts != command->end())
			{
				this->allocate
				(
					command->value("unit", 0), command->value("parts", 0),
					command->value("rem", 0), *lifts
				);
			} else if(auto id = command->find("read"); id != command->end())
			{
				this->read_contents(*id);
			} else if(auto id = command->find("write"); id != command->end())
			{
				this->write_contents(*id, command->at("value"));
			} else if(auto id = command->find("lift"); id != command->end())
			{
				this->lift(*id);
			} else if(auto id = command->find("drop"); id != command->end())
			{
				this->drop(*id);
			} else
			{
				this->fail_bad_request();
			}
		} catch(const json::exception&)
		{
			this->fail_bad_request();
		}

		return !this->is_lost();
	}
	
	void server_session::authorize(const nlohmann::json& input)
	{
		char hash_bytes[sizeof(std::uint64_t[2])];
		if(!deserialize_octets(input, hash_bytes, sizeof hash_bytes))
		{
			this->fail_bad_request();
		} else
		{
			secret_hash hash{0, 0};
			for(unsigned i = 0; i < sizeof hash_bytes; ++i)
			{
				auto& half = i < sizeof(std::uint64_t) ? hash.first : hash.second;
				half = half << 8 | static_cast<std::uint8_t>(hash_bytes[i]);
			}

			this->send(this->authorized = hash == this->secret.get());
		}
	}

	void server_session::finalize()
	{
		ce2103::linked_list<std::size_t> stale_ids;
		for(const auto& [id, pair] : this->objects)
		{
			stale_ids.append(id);
		}

		if(stale_ids.get_size() > 0)
		{
			this->send({{"leaked", std::vector<std::size_t>(stale_ids.begin(), stale_ids.end())}});
		} else
		{
			this->send_empty();
		}

		this->discard();
	}

	void server_session::allocate
	(
		std::size_t part_size, std::size_t parts,
		std::size_t remainder, std::size_t initial_count
	)
	{
		if((part_size == 0 || parts == 0) && remainder == 0)
		{
			this->fail_wrong_size();
			return;
		}

		auto& gc = garbage_collector::get_instance();
		gc.require_contiguous_ids((part_size > 0 ? parts : 0) + (remainder > 0 ? 1 : 0));

		std::optional<std::size_t> first_id;
		auto allocate_next = [&, this](std::size_t size)
		{
			auto [id, resource, base] = gc.allocate_of<char>(size);
			if(!first_id)
			{
				first_id = id;
			}

			this->objects.insert(id, std::make_pair(base, size));
		};
	
		for(std::size_t i = 0; i < parts; ++i)
		{
			allocate_next(part_size);
		}

		allocate_next(remainder);
		while(initial_count-- > 0)
		{
			gc.lift(*first_id);
		}

		this->send(*first_id);
	}

	void server_session::lift(std::size_t id)
	{
		if(this->expect_extant(id) != nullptr)
		{
			garbage_collector::get_instance().lift(id);
			this->send_empty();
		}
	}

	void server_session::drop(std::size_t id)
	{
		if(this->expect_extant(id) != nullptr)
		{
			switch(garbage_collector::get_instance().drop(id))
			{
				case drop_result::hanging:
					this->send({{"hanging", true}});
					break;

				case drop_result::lost:
					this->send({{"lost", true}});
					this->objects.remove(id);

					break;

				default:
					this->send_empty();
			}
		}
	}

	void server_session::read_contents(std::size_t id)
	{
		if(auto* pair = this->expect_extant(id); pair != nullptr)
		{
			auto [base, size] = *pair;
			this->send(serialize_octets(std::string_view{base, size}));
		}
	}

	void server_session::write_contents(std::size_t id, const nlohmann::json& contents)
	{
		if(auto* pair = this->expect_extant(id); pair != nullptr)
		{
			auto [base, size] = *pair;
			if(!deserialize_octets(contents, base, size))
			{
				this->fail_wrong_size();
			} else
			{
				this->send_empty();
			}
		}
	}

	std::pair<char*, std::size_t>* server_session::expect_extant(std::size_t id) noexcept
	{
		auto* pair = this->objects.search(id);
		if(pair == nullptr)
		{
			this->send_error("object not found");
		}

		return pair;
	}

	void server_session::send_empty()
	{
		this->send(json({}));
	}

	void server_session::send_error(const char* message)
	{
		this->send({{"error", message}});
	}

	void server_session::fail_bad_request()
	{
		this->send_error("bad request");
	}

	void server_session::fail_wrong_size()
	{
		this->send_error("wrong size");
	}
}

//! Language-level process entrypoint.
int main(int argc, const char* const argv[])
{
	ce2103::mm::initialize_local();

	std::optional<ce2103::ip_endpoint> endpoint = std::nullopt;
	if(argc != 2 || (endpoint = ce2103::ip_endpoint::try_from(argv[1]), !endpoint))
	{
		std::cerr << "Usage: " << argv[0] << " <address>:<port>\n";
		return 1;
	}

	const char* plain_text_secret;
	if((plain_text_secret = std::getenv("MM_PSK")) == nullptr)
	{
		std::cerr << argv[0] << "error: provide the MM_PSK (password) environment variable\n";
		return 1;
	}

	auto secret = ce2103::md5::of(plain_text_secret);

	ce2103::socket listen_socket;
	if(!listen_socket.bind(*endpoint, true))
	{
		std::cerr << "Error: failed to bind the listening socket\n";
		return 1;
	}

	ce2103::reactor{std::move(listen_socket), [&](ce2103::socket client) {
		return server_session{std::move(client), secret};
	}}.run();
}

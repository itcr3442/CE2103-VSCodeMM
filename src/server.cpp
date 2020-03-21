#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <algorithm>
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

			~server_session();

			//! Reads a command and processes it.
			void handle_command();

		private:
			//! MD5 hash of the preshared key
			const secret_hash& secret;

			//! ID-to-(base, size) map of active objects for this session.
			ce2103::hash_map<std::size_t, std::pair<char*, std::size_t>> objects;

			//! Whether the client has been authorized
			bool authorized = false;

			//! Attempts to authorize the client with the given PSK hash.
			void authorize(std::string&& input);

			//! Finalizes the session.
			void finalize();

			//! Allocates a new region of memory.
			void allocate(std::size_t size);

			//! Incremnts an object's reference count.
			void lift(std::size_t id);

			//! Decrements an object's reference count.
			void drop(std::size_t id);

			//! Dumps the given object's memory contents to the client.
			void read_contents(std::size_t id);

			//! Overwrites an object's memory contents.
			void write_contents(std::size_t id, std::string&& contents);

			//! Retrieves an active pair (see "objects"), otherwise reports client failure.
			std::pair<char*, std::size_t>* expect_extant(std::size_t id) noexcept;

			//! Reports success to the client with a response value.
			template<typename T>
			void send_result(T value);

			//! Sends an empty response ("{}")
			void send_empty();

			//! Reports success to the client with a response value.
			void send_result(long value);

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
			while(garbage_collector::get_instance().drop(id) > 0)
			{
				continue;
			}
		}
	}

	void server_session::handle_command()
	{
		auto command = this->receive();
		if(!command)
		{
			this->fail_bad_request();
			this->discard();

			return;
		}

		try
		{
			const std::string& operation = command->at("op");
			if(operation == "auth")
			{
				this->authorize(command->at("hash"));
			} else if(operation == "bye")
			{
				this->finalize();
			} else if(!this->authorized)
			{
				this->send_error("unauthorized");
			} else if(operation == "alloc")
			{
				this->allocate(command->at("size"));
			} else
			{
				std::size_t id = command->at("id");
				if(operation == "read")
				{
					this->read_contents(id);
				} else if(operation == "write")
				{
					this->write_contents(id, command->at("value"));
				} else if(operation == "lift")
				{
					this->lift(id);
				} else if(operation == "drop")
				{
					this->drop(id);
				} else
				{
					this->fail_bad_request();
				}
			}
		} catch(const json::exception&)
		{
			this->fail_bad_request();
		}
	}
	
	void server_session::authorize(std::string&& input)
	{
		if(!deserialize_octets(input, sizeof(std::uint64_t[2])))
		{
			this->fail_bad_request();
		} else
		{
			secret_hash hash{0, 0};
			for(unsigned i = 0; i < sizeof(std::uint64_t[2]); ++i)
			{
				auto& half = i < sizeof(std::uint64_t) ? hash.first : hash.second;
				half = half << 8 | static_cast<std::uint8_t>(input[i]);
			}

			this->send_result(this->authorized = hash == this->secret);
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

	void server_session::allocate(std::size_t size)
	{
		auto [id, resource, base] = garbage_collector::get_instance().allocate_of<char>(size);
		this->objects.insert(id, std::make_pair(base, size));
	
		this->send_result(id);
	}

	void server_session::lift(std::size_t id)
	{
		if(this->expect_extant(id) != nullptr)
		{
			this->send_result(garbage_collector::get_instance().lift(id));
		}
	}

	void server_session::drop(std::size_t id)
	{
		if(this->expect_extant(id) != nullptr)
		{
			auto remaining = garbage_collector::get_instance().drop(id);
			if(remaining == 0)
			{
				this->objects.remove(id);
			}

			this->send_result(remaining);
		}
	}

	void server_session::read_contents(std::size_t id)
	{
		if(auto* pair = this->expect_extant(id); pair != nullptr)
		{
			auto [base, size] = *pair;

			std::string contents;
			contents.reserve(size * 2);
			contents.resize(size);

			std::copy(base, base + size, &contents[0]);
			serialize_octets(contents);

			this->send_result(std::move(contents));
		}
	}

	void server_session::write_contents(std::size_t id, std::string&& contents)
	{
		if(auto* pair = this->expect_extant(id); pair != nullptr)
		{
			auto [base, size] = *pair;
			if(!deserialize_octets(contents, size))
			{
				this->fail_wrong_size();
			} else
			{
				std::copy(&contents[0], &contents[size], base);
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

	template<typename T>
	void server_session::send_result(T value)
	{
		this->send({{"value", std::move(value)}});
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
	using ce2103::ip_endpoint, ce2103::socket;

	ce2103::mm::initialize_local();

	std::optional<ip_endpoint> endpoint = std::nullopt;
	if(argc != 2 || (endpoint = ip_endpoint::try_from(argv[1]), !endpoint))
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

	socket listen_socket;
	if(!listen_socket.bind(*endpoint, true))
	{
		std::cerr << "Error: failed to bind the listening socket\n";
		return 1;
	}

	auto secret = ce2103::md5::of(plain_text_secret);

	while(true)
	{
		std::optional<socket> client = listen_socket.accept();
		if(!client)
		{
			std::cerr << "Error: failed to accept a client connection\n";
			return 1;
		}

		server_session single_client{std::move(*client), secret};
		while(!single_client.is_lost())
		{
			single_client.handle_command();
		}
	}
}

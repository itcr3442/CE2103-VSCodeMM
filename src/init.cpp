#include <mutex>
#include <string>
#include <variant>
#include <utility>
#include <cstdlib>
#include <cstddef>
#include <sstream>
#include <optional>
#include <iostream>

#include "ce2103/network.hpp"

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/init.hpp"
#include "ce2103/mm/debug.hpp"
#include "ce2103/mm/client.hpp"
#include "ce2103/mm/session.hpp"

using ce2103::mm::session;
using ce2103::mm::_detail::debug_chain;

namespace
{
	//! Active session used as a side channel for debugging
	class debug_session : public session
	{
		public:
			//! Constructs a debug session out of a socket
			inline debug_session(ce2103::socket client_socket)
			: session{std::move(client_socket)}
			{}

			//! Sends a debug message line, constructed from a chain
			void put(debug_chain* node);

			/*!
			 * \brief The debug server might indicate some environemnt
			 *        variables to set: MM_PSK and MM_SERVER. This occurs
			 *        during the handshake.
			 *
			 * \return whether no error ocurred
			 */
			bool accept_options();
	};

	//! Debug channel in use, if available
	std::optional<debug_session> debug_logger;

	//! Guarantees the initialization functions are called at most once
	std::once_flag initialization_flag;

	//! Manager which corresponds to at::any
	ce2103::mm::memory_manager* default_manager;

	void debug_session::put(debug_chain* last)
	{
		nlohmann::json entry({});
		while(last != nullptr)
		{
			if(auto* as_string = std::get_if<std::string>(&last->value);
			   as_string != nullptr)
			{
				entry[std::move(last->key)] = std::move(*as_string);
			} else if(auto* as_unsigned = std::get_if<std::size_t>(&last->value);
			   as_unsigned != nullptr)
			{
				entry[std::move(last->key)] = *as_unsigned;
			} else
			{
				entry[std::move(last->key)] = std::get<bool>(last->value);
			}

			last = last->previous;
		}

		this->send(std::move(entry));
	}

	bool debug_session::accept_options()
	{
		try
		{
			auto options = this->receive();

			auto update_environment = [&](const char* key, const char* option)
			{
				if(auto value = options->find(option); value != options->end())
				{
					::setenv(key, static_cast<const std::string&>(*value).c_str(), true);
				}
			};

			if(options)
			{
				update_environment("MM_SERVER", "server");
				update_environment("MM_PSK", "psk");
			}

			return true;
		} catch(...)
		{
			// Fall-through
		}

		return false;
	}

	//! Initializes the debug channel, if available
	void initialize_debug()
	{
		using ce2103::socket, ce2103::ip_endpoint;

		const char* endpoint_string = std::getenv("MM_DEBUG_TARGET");
		if(endpoint_string == nullptr)
		{
			return;
		}

		socket client_socket;
		if(auto endpoint = ip_endpoint::try_from(endpoint_string);
		   endpoint && client_socket.connect(*endpoint))
		{
			debug_logger.emplace(std::move(client_socket));
			if(!debug_logger->accept_options())
			{
				debug_logger.reset();
			}
		}

		if(!debug_logger)
		{
			std::cerr << "=== Debug connection failed ===\n";
		}
	}

	// Setups the local GC
	void set_local_default()
	{
		// This starts the GC thread.
		default_manager = &ce2103::mm::garbage_collector::get_instance();
	}
}

namespace ce2103::mm
{
	_detail::debug_chain::debug_chain(debug_chain* previous, std::string key, void* value) noexcept
	: previous{previous}, key{std::move(key)}, value{std::string{}}
	{
		std::ostringstream stream;
		stream << value;

		this->value = stream.str();
	}

	void _detail::debug_log(debug_chain* last)
	{
		if(debug_logger)
		{
			debug_logger->put(last);
		}
	}

	void initialize_local()
	{
		std::call_once(initialization_flag, []()
		{
			initialize_debug();
			set_local_default();
		});
	}

	void initialize()
	{
		auto perform = []()
		{
			using ce2103::mm::remote_manager, ce2103::socket, ce2103::ip_endpoint;

			// Attempt to establish an authorized session with the server
			bool connected = false;
			if(const char* endpoint_string = std::getenv("MM_SERVER");
			   endpoint_string != nullptr)
			{
				socket client_socket;
				if(auto endpoint = ip_endpoint::try_from(endpoint_string); !endpoint)
				{
					std::cerr << "=== Invalid endpoint '" << endpoint_string << "' ===\n";
				} else if(const char* key = std::getenv("MM_PSK"); key == nullptr)
				{
					std::cerr << "=== MM_SERVER is set but not MM_PSK ===\n";
				} else if(!client_socket.connect(*endpoint))
				{
					std::cerr << "=== Connection to server failed ===\n";
				} else if(!remote_manager::initialize(std::move(client_socket), key))
				{
					std::cerr << "=== Handshake failed (wrong MM_PSK?) ===\n";
				} else
				{
					connected = true;
					default_manager = &remote_manager::get_instance();
				}

				if(!connected)
				{
					std::cerr << "=== Remote setup failed, falling back to local services ===\n";
				}

				_detail::debug_log("connect", "success", connected);
			}

			if(!connected)
			{
				set_local_default();
			}
		};

		std::call_once(initialization_flag, [&]()
		{
			initialize_debug();
			perform();
		});
	}

	memory_manager& memory_manager::get_default(at storage) noexcept
	{
		switch(storage)
		{
			case at::local:
				return garbage_collector::get_instance();

			case at::remote:
				return remote_manager::get_instance();

			case at::any:
			default:
				return *default_manager;
		}
	}
}

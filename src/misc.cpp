#include <mutex>
#include <string>
#include <utility>
#include <cstdlib>
#include <iostream>
#include <system_error>

#include "ce2103/network.hpp"

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/init.hpp"
#include "ce2103/mm/error.hpp"
#include "ce2103/mm/client.hpp"

namespace
{
	std::once_flag initialization_flag;

	ce2103::mm::memory_manager* default_manager;

	//! Unlocked body of initialize_local().
	void do_initialize_local()
	{
		// This starts the GC thread.
		default_manager = &ce2103::mm::garbage_collector::get_instance();
	}

	//! Unlocked body of initialize().
	void do_initialize()
	{
		using ce2103::mm::remote_manager, ce2103::socket, ce2103::ip_endpoint;

		bool connected = false;
		if(const char* endpoint_string = std::getenv("MM_SERVER"); endpoint_string != nullptr)
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
		}

		if(!connected)
		{
			do_initialize_local();
		}
	}
}

namespace ce2103::mm
{
	void initialize_local()
	{
		std::call_once(initialization_flag, do_initialize_local);
	}

	void initialize()
	{
		std::call_once(initialization_flag, do_initialize);
	}

	memory_manager& memory_manager::get_default() noexcept
	{
		return *default_manager;
	}

	const std::error_category& error_category::get() noexcept
	{
		static const error_category category;
		return category;
	}

	const char* error_category::name() const noexcept
	{
		return "ce2103::mm";
	}

	std::string error_category::message(int condition) const
	{
		switch(static_cast<error_code>(condition))
		{
			case error_code::memory_error:
				return "memory bus error";

			case error_code::network_failure:
				return "remote memory operation failed";

			case error_code::null_dereference:
				return "null VSPtr<T> dereferenced";

			case error_code::out_of_bounds:
				return "VSPtr<T[]> index out of bounds";

			default:
				return "unknown error";
		}
	}
}

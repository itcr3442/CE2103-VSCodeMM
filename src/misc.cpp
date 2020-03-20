#include <mutex>
#include <string>
#include <utility>
#include <cstdlib>
#include <iostream>

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/init.hpp"

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
}

namespace ce2103::mm
{
	void initialize_local()
	{
		std::call_once(initialization_flag, do_initialize_local);
	}

	memory_manager& memory_manager::get_default() noexcept
	{
		return *default_manager;
	}
}

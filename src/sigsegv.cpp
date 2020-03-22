#include <cstddef>
#include <cassert>

#include "ce2103/mm/client.hpp"
 
namespace ce2103::mm
{
	void remote_manager::install_trap_region()
	{
		//TODO
		assert(false);
	}

	void* remote_manager::allocation_base_for(std::size_t) noexcept
	{
		//TODO
		assert(false);
		return nullptr;
	}

	void remote_manager::evict(std::size_t)
	{
		//TODO
		assert(false);
	}
}

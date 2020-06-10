#include <string>
#include <system_error>

#include "ce2103/mm/gc.hpp"
#include "ce2103/mm/error.hpp"
#include "ce2103/mm/vsptr.hpp"

namespace ce2103::mm
{
	[[noreturn]]
	void _detail::throw_null_dereference()
	{
		throw std::system_error{error_code::null_dereference};
	}

	[[noreturn]]
	void _detail::throw_out_of_bounds()
	{
		throw std::system_error{error_code::out_of_bounds};
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

			case error_code::no_remote_session:
				return "no remote session is active";

			default:
				return "unknown error";
		}
	}
}

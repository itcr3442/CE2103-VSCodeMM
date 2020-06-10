#include <string>
#include <cstdlib>
#include <typeinfo>

// GNU extension
#include <cxxabi.h>

#include "ce2103/rtti.hpp"

namespace ce2103
{
	std::string demangle(const std::type_info& type) noexcept
	{
		std::string output;
		if(char* demangled = ::abi::__cxa_demangle(type.name(), nullptr, nullptr, nullptr);
		   demangled != nullptr)
		{
			output = demangled;
			std::free(demangled);
		} else
		{
			output = "(\?\?\?)";
		}

		return output;
	}
}

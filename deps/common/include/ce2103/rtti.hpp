#ifndef CE2103_RTTI_HPP
#define CE2103_RTTI_HPP

#include <string>
#include <typeinfo>

namespace ce2103
{
	std::string demangle(const std::type_info& type) noexcept;
}

#endif


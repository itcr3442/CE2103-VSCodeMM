#ifndef CE2103_RTTI_HPP
#define CE2103_RTTI_HPP

#include <string>
#include <typeinfo>

namespace ce2103
{
	/*!
	 * \brief Produces a human-readable and standard representation of
	 *        a mangled RTTI type handle's name in accordance with the
	 *        the Itanium ABI.
	 *
	 * \param type type whose name is to be demangled
	 *
	 * \return demangled type name
	 */
	std::string demangle(const std::type_info& type) noexcept;
}

#endif


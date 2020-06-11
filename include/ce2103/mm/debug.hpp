#ifndef CE2103_MM_DEBUG_HPP
#define CE2103_MM_DEBUG_HPP

#include <string>
#include <variant>
#include <utility>
#include <cstddef>

namespace ce2103::mm::_detail
{
	//! Chain of key-value pairs for internal use of debug_log()
	struct debug_chain
	{
		// Previous node
		debug_chain* previous;

		//! Node key
		std::string key;

		//! Node value
		std::variant<std::string, std::size_t, bool> value;

		//! Constructs a node with a string value
		inline debug_chain(debug_chain* previous, std::string key, std::string value) noexcept
		: previous{previous}, key{std::move(key)}, value{std::move(value)}
		{}

		//! Constructs a node with an unsigned integer value
		inline debug_chain(debug_chain* previous, std::string key, std::size_t value) noexcept
		: previous{previous}, key{std::move(key)}, value{std::move(value)}
		{}

		//! Constructs a node with a boolean value
		inline debug_chain(debug_chain* previous, std::string key, bool value) noexcept
		: previous{previous}, key{std::move(key)}, value{std::move(value)}
		{}
	};

	//! Base case for debug_log() (see below)
	void debug_log(debug_chain* last);

	//! Intermediary case for debug_log() (see below)
	template<typename ValueType, typename... PairTypes>
	void debug_log(debug_chain* parent, std::string key, ValueType value, PairTypes&&... pairs)
	{
		debug_chain next{parent, std::move(key), std::move(value)};
		debug_log(&next, std::forward<PairTypes>(pairs)...);
	}

	/*!
	 * \brief Prints a message through the debug channel, if available.
	 *        The pair ("op": operation) is additionally inserted.
	 *
	 * \param operation operation identifier
	 * \param pairs     additional content
	 */
	template<typename... PairTypes>
	void debug_log(std::string operation, PairTypes&&... pairs)
	{
		debug_log
		(
			static_cast<debug_chain*>(nullptr), "op",
			std::move(operation), std::forward<PairTypes>(pairs)...
		);
	}
}

#endif

#ifndef CE2103_MM_DEBUG_HPP
#define CE2103_MM_DEBUG_HPP

#include <string>
#include <variant>
#include <utility>
#include <cstddef>

namespace ce2103::mm::_detail
{
	struct debug_chain
	{
		debug_chain* previous;

		std::string key;

		std::variant<std::string, std::size_t> value;

		inline debug_chain(debug_chain* previous, std::string key, std::string value) noexcept
		: previous{previous}, key{std::move(key)}, value{std::move(value)}
		{}

		inline debug_chain(debug_chain* previous, std::string key, std::size_t value) noexcept
		: previous{previous}, key{std::move(key)}, value{std::move(value)}
		{}
	};

	void debug_log(debug_chain* last);

	template<typename ValueType, typename... PairTypes>
	void debug_log(debug_chain* parent, std::string key, ValueType value, PairTypes&&... pairs)
	{
		debug_chain next{parent, std::move(key), std::move(value)};
		debug_log(&next, std::forward<PairTypes>(pairs)...);
	}

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

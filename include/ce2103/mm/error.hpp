#ifndef CE2103_MM_ERROR_HPP
#define CE2103_MM_ERROR_HPP

#include <string>
#include <type_traits>
#include <system_error>

namespace ce2103::mm
{
	enum class error_code
	{
		unknown = 1,
		memory_error,
		network_failure,
		null_dereference,
		out_of_bounds
	};

	class error_category : public std::error_category
	{
		public:
			static const std::error_category& get() noexcept;

			virtual const char* name() const noexcept final override;

			virtual std::string message(int condition) const final override;

		private:
			error_category() noexcept = default;
	};

	inline std::error_code make_error_code(error_code code)
	{
		return std::error_code{static_cast<int>(code), error_category::get()};
	}
}

namespace std
{
	template<>
	struct is_error_code_enum<ce2103::mm::error_code> : true_type
	{};
}

#endif

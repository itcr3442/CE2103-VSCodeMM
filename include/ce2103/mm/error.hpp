#ifndef CE2103_MM_ERROR_HPP
#define CE2103_MM_ERROR_HPP

#include <string>
#include <type_traits>
#include <system_error>

namespace ce2103::mm
{
	//! Set of all library-specific system errors
	enum class error_code
	{
		unknown = 1,
		memory_error,
		network_failure,
		null_dereference,
		out_of_bounds,
		no_remote_session
	};

	//! Error category for ce2103::mm::error_code
	class error_category : public std::error_category
	{
		public:
			//! Returns a quasi-singleton instance
			static const std::error_category& get() noexcept;

			//! Retrieves the category's name
			virtual const char* name() const noexcept final override;

			//! Returns the message string for the given error condition
			virtual std::string message(int condition) const final override;

		private:
			//! Constructs the error category object
			error_category() noexcept = default;
	};

	//! Required by the Standard
	inline std::error_code make_error_code(error_code code)
	{
		return std::error_code{static_cast<int>(code), error_category::get()};
	}
}

namespace std
{
	//! Required by the Standard
	template<>
	struct is_error_code_enum<ce2103::mm::error_code> : true_type
	{};
}

#endif

#ifndef CE2103_HASH_HPP
#define CE2103_HASH_HPP

#include <utility>
#include <cstdint>
#include <string_view>

namespace ce2103
{
	//! State machine for calculating MD5 hashes.
	class md5
	{
		public:
			/*!
			 * \brief Computes the MD5 hash of the given input.
			 *
			 * \param input message whose hash is to be calculated
			 *
			 * \return pair containing the upper and lower 64-bit halves of the hash
			 */
			static auto of(std::string_view input) noexcept
				-> std::pair<std::uint64_t, std::uint64_t>;

			//! Appends data to the source message.
			void feed(const void* source, std::size_t bytes) noexcept;

			//! Indicates end-of-input and returns the computed hash.
			decltype(of({})) finish() && noexcept;

		private:
			static const std::uint32_t SUM_TABLE[64];   //!< Internal constant table
			static const std::uint8_t  SHIFT_TABLE[64]; //!< Internal constant table

			//! Intermediary buffer for holding partial message blocks.
			std::uint8_t  buffer[sizeof SUM_TABLE / sizeof SUM_TABLE[0]];

			//! Usage of the previous buffer.
			std::uint64_t buffer_usage = 0;

			//! Total count of input bits provided to the state machine.
			std::uint64_t processed_bits = 0;

			std::uint32_t a = 0x67452301; //!< State register
			std::uint32_t b = 0xefcdab89; //!< State register
			std::uint32_t c = 0x98badcfe; //!< State register
			std::uint32_t d = 0x10325476; //!< State register

			//! Performs a hash round. Requires a full buffer.
			void do_round() noexcept;
	};
}

#endif

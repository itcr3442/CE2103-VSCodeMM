#include <utility>
#include <cstddef>
#include <climits>
#include <cassert>
#include <algorithm>
#include <string_view>

#include "ce2103/hash.hpp"

#if CHAR_BIT != 8
	#error Weird architectures are unsupported
#endif

namespace
{
	/*!
	 * \brief Swaps the bytes of an integer if the host is a big-endian
	 *        system, and preserves its input otherwise.
	 */
	template<typename T>
	static constexpr inline T as_little_endian(T integer) noexcept
	{
		T result = 0;
		for(unsigned i = 0; i < sizeof(T); ++i)
		{
			auto byte = reinterpret_cast<std::uint8_t*>(&integer)[i];
			result |= static_cast<T>(byte) << (8 * i);
		}

		return result;
	}
}

namespace ce2103
{
	std::pair<std::uint64_t, std::uint64_t> md5::of(std::string_view input) noexcept
	{
		md5 hasher;
		hasher.feed(input.data(), input.length());

		return std::move(hasher).finish();
	}

	void md5::feed(const void* source, std::size_t bytes) noexcept
	{
		auto total_bytes = bytes;

		const auto* input = static_cast<const std::uint8_t*>(source);
		while(bytes > 0)
		{
			std::size_t taken = std::min(sizeof this->buffer - this->buffer_usage, bytes);

			std::copy(input, &input[taken], &this->buffer[this->buffer_usage]);
			if((this->buffer_usage += taken) == sizeof this->buffer)
			{
				this->do_round();
			}

			bytes -= taken;
			input += taken;
		}

		this->processed_bits += 8 * total_bytes;
	}

	decltype(md5::of({})) md5::finish() && noexcept
	{
		std::uint64_t final_addend = as_little_endian(this->processed_bits);

		constexpr std::uint8_t one = 0b1000'0000;
		this->feed(&one, sizeof one);

		constexpr unsigned padding_end = sizeof this->buffer - sizeof this->processed_bits;
		if(this->buffer_usage > padding_end)
		{
			std::fill(&this->buffer[this->buffer_usage], &this->buffer[sizeof this->buffer], 0);
			this->buffer_usage = sizeof this->buffer;

			this->do_round();
		}

		std::fill(&this->buffer[this->buffer_usage], &this->buffer[padding_end], 0);
		this->buffer_usage = padding_end;

		this->feed(&final_addend, sizeof final_addend);
		assert(this->buffer_usage == 0);

		std::uint32_t final_hash[4] = {a, b, c, d};
		std::uint64_t output[2] = {0, 0};

		for(unsigned i = 0; i < sizeof output; ++i)
		{
			auto* target = &output[i >= (sizeof output) / 2];
			*target = *target << 8 | reinterpret_cast<std::uint8_t*>(final_hash)[i];
		}

		return std::make_pair(output[0], output[1]);
	}

	const std::uint32_t md5::SUM_TABLE[64] =
	{
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
		0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
		0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
		0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
		0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
		0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
		0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
		0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
		0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
	};

	const std::uint8_t md5::SHIFT_TABLE[64] =
	{
		0x07, 0x0c, 0x11, 0x16, 0x07, 0x0c, 0x11, 0x16,
		0x07, 0x0c, 0x11, 0x16, 0x07, 0x0c, 0x11, 0x16,
		0x05, 0x09, 0x0e, 0x14, 0x05, 0x09, 0x0e, 0x14,
		0x05, 0x09, 0x0e, 0x14, 0x05, 0x09, 0x0e, 0x14,
		0x04, 0x0b, 0x10, 0x17, 0x04, 0x0b, 0x10, 0x17,
		0x04, 0x0b, 0x10, 0x17, 0x04, 0x0b, 0x10, 0x17,
		0x06, 0x0a, 0x0f, 0x15, 0x06, 0x0a, 0x0f, 0x15,
		0x06, 0x0a, 0x0f, 0x15, 0x06, 0x0a, 0x0f, 0x15
	};

	void md5::do_round() noexcept
	{
		assert(this->buffer_usage == sizeof this->buffer);

		std::uint32_t a = this->a;
		std::uint32_t b = this->b;
		std::uint32_t c = this->c;
		std::uint32_t d = this->d;

		for(unsigned i = 0; i < sizeof this->buffer; ++i)
		{
			std::uint32_t f;
			std::uint32_t g;

			switch(i >> 4)
			{
				case 0b00:
					f = (b & c) | (~b & d);
					g = i;
					break;

				case 0b01:
					f = (d & b) | (~d & c);
					g = (5 * i + 1) & 0b1111;
					break;

				case 0b10:
					f = b ^ c ^ d;
					g = (3 * i + 5) & 0b1111;
					break;

				case 0b11:
					f = c ^ (b | ~d);
					g = (7 * i) & 0b1111;
					break;
			}

			auto word = as_little_endian(reinterpret_cast<std::uint32_t*>(this->buffer)[g]);

			f += a + SUM_TABLE[i] + word;
			a = d;
			d = c;
			c = b;

			auto shift = SHIFT_TABLE[i];
			b += (f << shift) | (f >> (8 * sizeof f - shift));
		}

		this->a += a;
		this->b += b;
		this->c += c;
		this->d += d;

		this->buffer_usage = 0;
	}

	std::uint32_t murmur3::of(std::string_view input) noexcept
	{
		murmur3 hasher;
		hasher.feed(input.data(), input.length());

		return std::move(hasher).finish();
	}

	void murmur3::feed(const void* input, std::size_t length) noexcept
	{
		const auto* byte_input = static_cast<const std::uint8_t*>(input);
		if(this->buffer_usage > 0)
		{
			std::size_t copied = std::min(length, sizeof this->buffer - this->buffer_usage);
			std::copy(byte_input, byte_input + copied, &this->buffer[this->buffer_usage]);

			this->do_round(*reinterpret_cast<std::uint32_t*>(this->buffer));

			length -= copied;
			byte_input += copied;
		}

		while(length >= sizeof this->buffer)
		{
			this->do_round(*reinterpret_cast<const std::uint32_t*>(byte_input));

			length -= sizeof this->buffer;
			byte_input += sizeof this->buffer;
		}

		std::copy(byte_input, byte_input + length, this->buffer);
		this->buffer_usage = length;
	}

	std::uint32_t murmur3::finish() && noexcept
	{
		std::uint32_t last = 0;
		for(std::size_t i = this->buffer_usage; i > 0; --i)
		{
			last = (last << 8) | buffer[i - 1];
		}

		this->hash = this->hash ^ scramble(last) ^ this->buffer_usage;
		this->hash = (this->hash ^ (this->hash >> 16)) * 0x85ebca6b;
		this->hash = (this->hash ^ (this->hash >> 13)) * 0xc2b2ae35;

		return hash ^ (hash >> 16);
	}

	std::uint32_t murmur3::scramble(std::uint32_t value) noexcept
	{
		return ((value << 15) | (value >> 17)) * 0x1b873593;
	}

	void murmur3::do_round(std::uint32_t block) noexcept
	{
		this->hash ^= scramble(block);
		this->hash = (this->hash << 13) | (this->hash >> 19);
		this->hash = this->hash * 5 + 0xe6546b64;
	}
}

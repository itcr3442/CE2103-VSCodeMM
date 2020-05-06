#include <vector>
#include <string>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <optional>
#include <algorithm>
#include <string_view>

#include "nlohmann/json.hpp"

#include "ce2103/mm/client.hpp"
#include "ce2103/mm/session.hpp"

using nlohmann::json;

namespace ce2103::mm
{
	json session::serialize_octets(std::string_view input)
	{
		auto get_nibble_char = [](std::uint8_t nibble) -> char
		{
			return nibble < 0x0a ? '0' + nibble : 'a' + nibble - 0x0a;
		};

		/* Breaking at less than three consecutive zeros will increase the
		 * serialized length instead of shortening it.
		 *
		 * Example (with least break of one zero):
		 *
		 *   ["aa0000bb"] (length 12) => ["aa",2,"bb"] (length 13)
		 *   ["aa000000bb"] (length 14) => ["aa",3,"bb"] (length 13)
		 */
		constexpr char least_break[] = {'\0', '\0', '\0'};

		std::vector<json> fragments;
		while(input.length() > 0)
		{
			auto barrier = input.find(least_break, 0, sizeof least_break);

			bool is_break = barrier == 0;
			if(is_break)
			{
				barrier = input.find_first_not_of('\0', sizeof least_break);
			}

			if(barrier == std::string_view::npos)
			{
				barrier = input.length();
			}

			if(is_break)
			{
				fragments.push_back(barrier);
			} else
			{
				std::string fragment;
				fragment.reserve(barrier * 2);

				for(char as_char : input.substr(0, barrier))
				{
					auto byte = static_cast<std::uint8_t>(as_char);

					fragment.push_back(get_nibble_char(byte >> 4));
					fragment.push_back(get_nibble_char(byte & 0b0000'1111));
				}

				fragments.push_back(std::move(fragment));
			}

			input.remove_prefix(barrier);
		}

		return json(std::move(fragments));
	}

	std::optional<std::size_t> session::deserialized_size(const json& input) noexcept
	{
		if(!input.is_array())
		{
			return std::nullopt;
		}

		std::size_t size = 0;
		for(const auto& fragment : input)
		{
			if(fragment.is_number_unsigned())
			{
				size += static_cast<std::size_t>(fragment);
			} else if(fragment.is_string())
			{
				std::size_t length = fragment.get_ref<const std::string&>().length();
				if(length % 2 != 0)
				{
					return std::nullopt;
				}

				size += length / 2;
			} else
			{
				return std::nullopt;
			}
		}

		return size;
	}

	bool session::deserialize_octets(const json& input, char* output, std::size_t bytes) noexcept
	{
		auto get_nibble = [](char digit) -> std::uint8_t
		{
			if(digit >= '0' && digit <= '9')
			{
				return digit - '0';
			} else if(digit >= 'a' && digit <= 'f')
			{
				return digit - 'a' + 0x0a;
			}

			__builtin_unreachable();
		};

		if(!input.is_array())
		{
			return false;
		}

		for(const auto& fragment : input)
		{
			bool is_break;
			std::size_t length;
			std::size_t fragment_length;

			if(fragment.is_number_unsigned())
			{
				is_break = true;
				length = fragment;
			} else if(fragment.is_string())
			{
				is_break = false;
				fragment_length = fragment.get_ref<const std::string&>().length();
				length = fragment_length / 2;
			} else
			{
				return false;
			}

			if(length > bytes || (!is_break && fragment_length != length * 2))
			{
				return false;
			}

			if(is_break)
			{
				std::fill(output, output + length, '\0');
			} else
			{
				const char* source = fragment.get_ref<const std::string&>().data();
				for(std::size_t i = 0; i < length; ++i)
				{
					std::uint8_t first = get_nibble(source[2 * i]);
					std::uint8_t second = get_nibble(source[2 * i + 1]);

					if(first > 0b000'1111 || second > 0b0000'1111)
					{
						return false;
					}

					output[i] = static_cast<char>(first << 4 | second);
				}
			}

			output += length;
			bytes -= length;
		}

		return bytes == 0;
	}

	void session::send(json data)
	{
		if(this->peer)
		{
			std::string line = data.dump();
			line.push_back('\n');

			this->peer->write(line);
		}
	}

	std::optional<json> session::receive()
	{
		std::string line;
		json data;

		if(this->peer && this->peer->read_line(line)
		&& !(data = json::parse(line, nullptr, false)).is_discarded())
		{
			return data;
		}

		return std::nullopt;
	}
}

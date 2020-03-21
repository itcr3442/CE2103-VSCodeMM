#include <string>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <optional>

#include "nlohmann/json.hpp"

#include "ce2103/mm/client.hpp"
#include "ce2103/mm/session.hpp"

using nlohmann::json;

namespace ce2103::mm
{
	void session::serialize_octets(std::string& input)
	{
		auto bytes = input.length();
		input.resize(bytes * 2);

		auto get_nibble_char = [](std::uint8_t nibble) -> char
		{
			return nibble < 0x0a ? '0' + nibble : 'a' + nibble - 0x0a;
		};

		for(std::size_t i = bytes; i > 0; --i)
		{
			auto byte = static_cast<std::uint8_t>(input[i - 1]);

			input[2 * i - 1] = get_nibble_char(byte & 0b0000'1111);
			input[2 * i - 2] = get_nibble_char(byte >> 4);
		}
	}

	bool session::deserialize_octets(std::string& input, std::size_t bytes) noexcept
	{
		if(input.length() != bytes * 2)
		{
			return false;
		}

		auto get_nibble = [](char digit) -> std::uint8_t
		{
			if(digit >= '0' && digit <= '9')
			{
				return digit - '0';
			} else if(digit >= 'a' && digit <= 'f')
			{
				return digit - 'a' + 0x0a;
			}

			return 42;
		};

		for(std::size_t i = 0; i < bytes; ++i)
		{
			std::uint8_t first = get_nibble(input[2 * i]);
			std::uint8_t second = get_nibble(input[2 * i + 1]);

			if(first > 0b000'1111 || second > 0b0000'1111)
			{
				return false;
			}

			input[i] = static_cast<char>(first << 4 | second);
		}

		input.resize(bytes);
		return true;
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

	std::optional<nlohmann::json> session::receive()
	{
		std::string line;
		nlohmann::json data;

		if(this->peer && this->peer->read_line(line)
		&& !(data = nlohmann::json::parse(line, nullptr, false)).is_discarded())
		{
			return data;
		}

		return std::nullopt;
	}
}

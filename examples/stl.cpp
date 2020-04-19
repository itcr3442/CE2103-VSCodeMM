#include <cmath>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <functional>
#include <unordered_map>

#include "ce2103/hash_map.hpp"

#include "ce2103/mm/init.hpp"
#include "ce2103/mm/allocator.hpp"

template<typename T>
using managed_allocator = ce2103::mm::allocator<T>;

using char_traits           = std::char_traits<char>;
using char_allocator        = managed_allocator<char>;
using managed_string        = std::basic_string<char, char_traits, char_allocator>;
using managed_ostringstream = std::basic_ostringstream<char, char_traits, char_allocator>;

/* Cannot use std::unordered_map here because of a libstdc++ bug.
 * See <https://gcc.gnu.org/bugzilla/show_bug.cgi?id=57272>.
 */
template<typename K, typename V, class Hash = std::hash<K>>
using managed_hash_map = ce2103::hash_map<K, V, Hash, managed_allocator<std::pair<const K, V>>>;

template<typename T>
using managed_vector = std::vector<T, managed_allocator<T>>;

void print(int integer)
{
	std::cout << integer;
}

void print(const managed_string& string)
{
	std::cout << '"' << string << '"';
}

template<typename T>
void print(const managed_vector<T>& vector)
{
	std::cout << '[';

	bool is_first_element = true;
	for(const T& element : vector)
	{
		if(is_first_element)
		{
			is_first_element = false;
		} else
		{
			std::cout << ", ";
		}

		print(element);
	}

	std::cout << ']';
}

template<typename K, typename V, class Compare>
void print(const managed_hash_map<K, V, Compare>& map)
{
	std::cout << '{';

	bool is_first_element = true;
	for(const auto& [key, value] : map)
	{
		if(is_first_element)
		{
			is_first_element = false;
		} else
		{
			std::cout << ", ";
		}

		print(key);
		std::cout << ": ";
		print(value);
	}

	std::cout << '}';
}

int main()
{
	ce2103::mm::initialize();

	//managed_vector<managed_vector<managed_string>> container;
	managed_hash_map<int, managed_vector<managed_string>> container;
	for(int i = 0; i < 4; ++i)
	{
		auto& vector = container[i];
		for(int j = 0; j < (1 << i); ++j)
		{
			managed_ostringstream stream;

			stream << (std::log(42 + std::exp(~i ^ ~j)) * std::cos((1 << i) + j));
			vector.push_back(stream.str());
		}

		if(i % 2 == 0)
		{
			std::reverse(vector.begin(), vector.end());
		}
	}

	print(container);
	std::cout << '\n';
}

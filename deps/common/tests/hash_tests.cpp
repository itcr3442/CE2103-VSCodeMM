#include <utility>
#include <string_view>

#include "catch.hpp"
#include "ce2103/hash.hpp"

// This file replicates the test suite found in RFC 1321.

using ce2103::md5;

auto md5_pair(std::uint64_t upper, std::uint64_t lower)
{
	return std::make_pair(upper, lower);
}

TEST_CASE("md5(\"\")", "[hash][md5]")
{
	REQUIRE
	(
		   md5::of("")
		== md5_pair(0xd41d8cd98f00b204, 0xe9800998ecf8427e)
	);
}

TEST_CASE("md5(\"a\")", "[hash][md5]")
{
	REQUIRE
	(
		   md5::of("a")
		== md5_pair(0x0cc175b9c0f1b6a8, 0x31c399e269772661)
	);
}

TEST_CASE("md5(\"abc\")", "[hash][md5]")
{
	REQUIRE
	(
		   md5::of("abc")
		== md5_pair(0x900150983cd24fb0, 0xd6963f7d28e17f72)
	);
}

TEST_CASE("md5(\"message digest\")", "[hash][md5]")
{
	REQUIRE
	(
		   md5::of("message digest")
		== md5_pair(0xf96b697d7cb7938d, 0x525a2f31aaf161d0)
	);
}

TEST_CASE("md5(\"abcdefghijklmnopqrstuvwxyz\")", "[hash][md5]")
{
	REQUIRE
	(
		   md5::of("abcdefghijklmnopqrstuvwxyz")
		== md5_pair(0xc3fcd3d76192e400, 0x7dfb496cca67e13b)
	);
}

TEST_CASE("md5(<very long alphabetic string>)", "[hash][md5]")
{
	REQUIRE
	(
		   md5::of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")
		== md5_pair(0xd174ab98d277d9f5, 0xa5611c2c9f419d9f)
	);
}

TEST_CASE("md5(<very long numeric string>)\")", "[hash][md5]")
{
	REQUIRE
	(
		   md5::of("12345678901234567890123456789012345678901234567890123456789012345678901234567890")
		== md5_pair(0x57edf4a22be3c955, 0xac49da2e2107b67a)
	);
}

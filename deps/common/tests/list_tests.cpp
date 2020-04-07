#include "catch.hpp"
#include "ce2103/list.hpp"

using ce2103::linked_list;

SCENARIO("linked lists work correctly", "[list]")
{
	linked_list<int> list;

	GIVEN("an empty list")
	{
		REQUIRE(list.get_size() == 0);
		REQUIRE(list.begin() == list.end());

		WHEN("some stuff is added")
		{
			for(int i = 0; i < 3; ++i)
			{
				list.append(3 * i - 2);
			}

			THEN("the size changes")
			{
				REQUIRE(list.get_size() == 3);
				REQUIRE(list.begin() != list.end());

				AND_THEN("the inserted values are read out in order")
				{
					int i = 0;
					
					auto iterator = list.begin();
					while(iterator != list.end())
					{
						REQUIRE(*iterator++ == 3 * i++ - 2);
					}

					REQUIRE(i == 3);
				}
			}
		}
	}
}

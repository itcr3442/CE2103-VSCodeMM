#include "catch.hpp"

#include "ce2103/mm/init.hpp"
#include "ce2103/mm/vsptr.hpp"


#include <iostream>

#include "ce2103/mm/init.hpp"
#include "ce2103/mm/vsptr.hpp"

SCENARIO("basic VSPtr<T> usage", "[mm]")
{
	using ce2103::mm::VSPtr;
	ce2103::mm::initialize();

	GIVEN("two VSPtr<int>s")
	{
		VSPtr<int> myPtr = VSPtr<int>::New();
		VSPtr<int> myPtr2 = VSPtr<int>::New();

		REQUIRE(myPtr != myPtr2);

		WHEN("a pointer's object is assigned")
		{
			*myPtr = 5;
			THEN("the valuee changes")
			{
				REQUIRE(*myPtr == 5);
			}

			WHEN("two pointers share a value")
			{
				myPtr2 = myPtr;

				THEN("the pointers are the same")
				{
					REQUIRE(myPtr2 == myPtr);
				}

				WHEN("one of them is dereference-assigned")
				{
					myPtr = 6;

					THEN("the other one reflects this change")
					{
						REQUIRE(*myPtr2 == 6);
					}
				}
			}
		}
	}
}

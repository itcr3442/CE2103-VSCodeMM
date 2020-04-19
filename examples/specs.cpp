#include <iostream>

#include "ce2103/mm/init.hpp"
#include "ce2103/mm/vsptr.hpp"

int main()
{
	using ce2103::mm::VSPtr;

	ce2103::mm::initialize();

	VSPtr<int> myPtr = VSPtr<int>::New();
	VSPtr<int> myPtr2 = VSPtr<int>::New();
	*myPtr = 5;
	myPtr2 = myPtr;

	int valor = &myPtr;
	myPtr = 6;
	int valor2 = &myPtr2;

	std::cout << "&*myPtr  = " << &*myPtr << "\n&*myPtr2 = " << &*myPtr2
	          << "\nvalor  = " << valor << "\nvalor2 = " << valor2 << '\n';
}

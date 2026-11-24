#include "CoreTests.h"

#include <exception>
#include <iostream>

std::vector<CoreTestCase>& coreTests()
{
	static std::vector<CoreTestCase> tests;
	return tests;
}

int main()
{
	int failed = 0;
	for (const CoreTestCase& test : coreTests())
	{
		try
		{
			test.run();
			std::cout << "PASS " << test.name << "\n";
		}
		catch (const CoreTestFailure& failure)
		{
			std::cout << "FAIL " << test.name << ": " << failure.message << "\n";
			++failed;
		}
		catch (const std::exception& e)
		{
			std::cout << "FAIL " << test.name << ": exception " << e.what() << "\n";
			++failed;
		}
	}
	std::cout << "CORE TOTAL " << coreTests().size() << " tests, " << failed << " failed\n";
	return failed;
}

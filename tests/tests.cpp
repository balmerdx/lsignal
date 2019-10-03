#include "tests.h"

const char* TestRunner::m_testName;

void TestRunner::StartTest(const char *testName)
{
	m_testName = testName;
}

void TestRunner::EndTest()
{
	m_testName = "";
}

const char* TestRunner::CurrentTest()
{
	return m_testName;
}


void AssertHelper::VerifyValue(int expected, int actual, const char *message)
{
	if (expected != actual)
	{
		throw std::logic_error(MakeString("\n\n  %s\n\n    Excepted: %d\n    Actual: %d", message, expected, actual));
	}
}

void AssertHelper::VerifyValue(bool expected, bool actual, const char *message)
{
	if (expected != actual)
	{
		throw std::logic_error(MakeString("\n\n  %s\n\n    Excepted: %s\n    Actual: %s", message, expected ? "true" : "false", actual ? "true" : "false"));
	}
}

void ExecuteTest(std::function<void()> testMethod)
{
	try
	{
		testMethod();

		std::cout << "(*) Test " << TestRunner::CurrentTest() << " passed.";
	}
	catch (const std::exception &ex)
	{
		std::cout << "(!) Test " << TestRunner::CurrentTest() << " failed: " << ex.what() << "\n";
	}

	TestRunner::EndTest();

	std::cout << "\n";
}



int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	CallBasicTests();
	CallMultithreadTests();
	//std::cin.get();

	return 0;
}

#include "tests.h"

const char* TestRunner::m_testName;
int TestRunner::m_passedCount = 0;
int TestRunner::m_failedCount = 0;

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

void TestRunner::ReportPassed()
{
	m_passedCount++;
}

void TestRunner::ReportFailed()
{
	m_failedCount++;
}

int TestRunner::Summary()
{
	int total = m_passedCount + m_failedCount;
	std::cout << "\npassed " << m_passedCount << "/" << total << "\n";
	return m_failedCount == 0 ? 0 : 1;
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

void AssertHelper::VerifyTrue(bool condition, const char *message, const char *file, int line)
{
	if (!condition)
	{
		std::ostringstream oss;
		oss << "\n\n  " << file << ":" << line << "  " << message;
		throw std::logic_error(oss.str());
	}
}

void ExecuteTest(std::function<void()> testMethod)
{
	try
	{
		testMethod();

		std::cout << "(*) Test " << TestRunner::CurrentTest() << " passed.";
		TestRunner::ReportPassed();
	}
	catch (const std::exception &ex)
	{
		std::cout << "(!) Test " << TestRunner::CurrentTest() << " failed: " << ex.what() << "\n";
		TestRunner::ReportFailed();
	}
	catch (...)
	{
		std::cout << "(!) Test " << TestRunner::CurrentTest() << " failed: unknown exception\n";
		TestRunner::ReportFailed();
	}

	TestRunner::EndTest();

	std::cout << "\n";
}



int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	CallBasicTests();
	CallApiTests();
	CallLifetimeTests();
	CallMultithreadTests();
	//std::cin.get();

	return TestRunner::Summary();
}

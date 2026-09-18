#pragma once
#include "../lsignal.h"

#include <cstdio>
#include <iostream>
#include <exception>
#include <stack>
#include <typeinfo>
#include <atomic>
#include <thread>
#include <assert.h>
#include <string>
#include <sstream>

#define MethodName __func__

template<typename... Args>
std::string MakeString(const std::string& format, Args... args)
{
	size_t size = snprintf(nullptr, 0, format.c_str(), args...) + 1;
	std::unique_ptr<char[]> buffer(new char[size]);
	snprintf(buffer.get(), size, format.c_str(), args...);

	return std::string(buffer.get(), buffer.get() + size - 1);
}


class TestRunner
{
public:
	static void StartTest(const char *testName);
	static void EndTest();
	static const char* CurrentTest();

	static void ReportPassed();
	static void ReportFailed();
	//Prints "passed N/M" summary, returns 0 if everything passed, 1 otherwise.
	static int Summary();
private:
	static const char* m_testName;
	static int m_passedCount;
	static int m_failedCount;
};

class AssertHelper
{
public:
	static void VerifyValue(int expected, int actual, const char *message);
	static void VerifyValue(bool expected, bool actual, const char *message);

	//Same as VerifyValue, but include file/line of the caller in the error message.
	template<typename T>
	static void VerifyEq(const T& expected, const T& actual, const char *message, const char *file, int line)
	{
		if (!(expected == actual))
		{
			std::ostringstream oss;
			oss << "\n\n  " << file << ":" << line << "  " << message
				<< "\n\n    Excepted: " << expected
				<< "\n    Actual: " << actual;
			throw std::logic_error(oss.str());
		}
	}

	static void VerifyTrue(bool condition, const char *message, const char *file, int line);
};

#define VERIFY_EQ(expected, actual, msg) AssertHelper::VerifyEq((expected), (actual), (msg), __FILE__, __LINE__)
#define VERIFY_TRUE(cond, msg) AssertHelper::VerifyTrue((cond), (msg), __FILE__, __LINE__)

void ExecuteTest(std::function<void()> testMethod);

void CallBasicTests();
void CallMultithreadTests();
void CallApiTests();
void CallLifetimeTests();

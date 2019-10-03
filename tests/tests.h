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
private:
	static const char* m_testName;

};

class AssertHelper
{
public:
	static void VerifyValue(int expected, int actual, const char *message);
	static void VerifyValue(bool expected, bool actual, const char *message);
};

void ExecuteTest(std::function<void()> testMethod);

void CallBasicTests();
void CallMultithreadTests();
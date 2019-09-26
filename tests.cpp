#include <cstdio>
#include <iostream>
#include <exception>
#include <stack>
#include <typeinfo>
#include "lsignal.h"

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
	static void StartTest(const char *testName)
	{
		m_testName = testName;
	}

	static void EndTest()
	{
		m_testName = "";
	}

	static const char* CurrentTest()
	{
		return m_testName;
	}

private:
	static const char* m_testName;

};

const char* TestRunner::m_testName;

class AssertHelper
{
public:
	static void VerifyValue(int expected, int actual, const char *message)
	{
		if (expected != actual)
		{
			throw std::logic_error(MakeString("\n\n  %s\n\n    Excepted: %d\n    Actual: %d", message, expected, actual));
		}
	}

	static void VerifyValue(bool expected, bool actual, const char *message)
	{
		if (expected != actual)
		{
			throw std::logic_error(MakeString("\n\n  %s\n\n    Excepted: %s\n    Actual: %s", message, expected ? "true" : "false", actual ? "true" : "false"));
		}
	}

};

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

struct SignalOwner : public lsignal::slot
{
};

void CreateSignal_SignalShouldBeUnlocked()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sg;

	AssertHelper::VerifyValue(sg.is_locked(), false, "Signal should be unlocked.");
}

void LockSignal_SignalShouldBeLocked()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sg;

	sg.set_lock(true);

	AssertHelper::VerifyValue(sg.is_locked(), true, "Signal should be locked.");
}

void UnlockSignal_SignalShouldBeUnlocked()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sg;

	sg.set_lock(false);

	AssertHelper::VerifyValue(sg.is_locked(), false, "Signal should be unlocked.");
}

void CallSignalWithoutConnections_SignalShouldBeCalled()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void(int, bool)> sg;

	int paramOne = 7;
	bool paramTwo = true;

	sg(paramOne, paramTwo);
}

void CallSignalWithSingleConnection_SignalShouldBeCalled()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void(int, bool)> sg;

	int paramOne = 7;
	bool paramTwo = true;

	bool receiverCalled = false;

	std::function<void(int, bool)> receiver = [=, &receiverCalled](int p0, bool p1)
	{
		receiverCalled = true;

		AssertHelper::VerifyValue(p0, paramOne, "First parameter should be as expected.");
		AssertHelper::VerifyValue(p1, paramTwo, "Second parameter should be as expected.");
	};

	sg.connect(receiver, nullptr);

	sg(paramOne, paramTwo);

	AssertHelper::VerifyValue(receiverCalled, true, "Receiver should be called.");
}

void CallSignalWithMultipleConnections_SignalShouldBeCalled()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void(int, bool)> sg;

	int paramOne = 7;
	bool paramTwo = true;

	unsigned char receiverCalledTimes = 0;

	std::function<void(int, bool)> receiver = [=, &receiverCalledTimes](int p0, bool p1)
	{
		++receiverCalledTimes;

		AssertHelper::VerifyValue(p0, paramOne, "First parameter should be as expected.");
		AssertHelper::VerifyValue(p1, paramTwo, "Second parameter should be as expected.");
	};

	sg.connect(receiver, nullptr);
	sg.connect(receiver, nullptr);

	sg(paramOne, paramTwo);

	AssertHelper::VerifyValue(receiverCalledTimes, 2, "Count of calls of receiver should be as expected.");
}

void SetSameOwnerToSeveralSignals_AllSignalsShouldBeNotifiedAboutOwnerDestruction()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sigOne;
	lsignal::signal<void()> sigTwo;

	bool receiverOneCalled = false;
	bool receiverTwoCalled = false;

	std::function<void()> receiverOne = [&receiverOneCalled]()
	{
		receiverOneCalled = true;
	};

	std::function<void()> receiverTwo = [&receiverTwoCalled]()
	{
		receiverTwoCalled = true;
	};

	{
		SignalOwner signalOwner;

		sigOne.connect(receiverOne, &signalOwner);
		sigTwo.connect(receiverTwo, &signalOwner);

		sigOne();
		sigTwo();

		AssertHelper::VerifyValue(receiverOneCalled, true, "First receiver should be called.");
		AssertHelper::VerifyValue(receiverTwoCalled, true, "Second receiver should be called.");
	}

	receiverOneCalled = false;
	receiverTwoCalled = false;

	sigOne();
	sigTwo();

	AssertHelper::VerifyValue(receiverOneCalled, false, "First receiver should not be called.");
	AssertHelper::VerifyValue(receiverTwoCalled, false, "Second receiver should not be called.");
}

void CreateSignalToSignalConnection_WhenFirstSignalIsDestroyed_SecondSignalShouldBeNotifed()
{
	TestRunner::StartTest(MethodName);

	bool receiverOneCalled = false;
	bool receiverTwoCalled = false;

	std::function<void()> receiverOne = [&receiverOneCalled]()
	{
		receiverOneCalled = true;
	};

	std::function<void()> receiverTwo = [&receiverTwoCalled]()
	{
		receiverTwoCalled = true;
	};

	lsignal::signal<void()> sigTwo;

	{
		lsignal::signal<void()> sigOne;

		sigOne.connect(receiverOne, nullptr);
		sigTwo.connect(receiverTwo, nullptr);

		sigOne.connect(&sigTwo);

		sigOne();

		AssertHelper::VerifyValue(receiverOneCalled, true, "First receiver should be called.");
		AssertHelper::VerifyValue(receiverTwoCalled, true, "Second receiver should be called.");
	}

	sigTwo();

	AssertHelper::VerifyValue(receiverTwoCalled, true, "Second receiver should be called.");
}

void CreateSignalToSignalConnection_WhenSecondSignalIsDestryoed_FirstSignalShouldBeNotifed()
{
	TestRunner::StartTest(MethodName);

	bool receiverOneCalled = false;
	bool receiverTwoCalled = false;

	std::function<void()> receiverOne = [&receiverOneCalled]()
	{
		receiverOneCalled = true;
	};

	std::function<void()> receiverTwo = [&receiverTwoCalled]()
	{
		receiverTwoCalled = true;
	};

	lsignal::signal<void()> sigOne;

	{
		lsignal::signal<void()> sigTwo;

		sigOne.connect(receiverOne, nullptr);
		sigTwo.connect(receiverTwo, nullptr);

		sigOne.connect(&sigTwo);

		sigOne();

		AssertHelper::VerifyValue(receiverOneCalled, true, "First receiver should be called.");
		AssertHelper::VerifyValue(receiverTwoCalled, true, "Second receiver should be called.");
	}

	receiverOneCalled = false;
	receiverTwoCalled = false;

	sigOne();

	AssertHelper::VerifyValue(receiverOneCalled, true, "First receiver should be called.");
	AssertHelper::VerifyValue(receiverTwoCalled, false, "Second receiver should not be called.");
}

//Test class signal/slot

static int receiveSigACount;
static int receiveSigBCount;

class TestA : public lsignal::slot
{
public:
	lsignal::signal<void(int data)> sigA;

	void ReceiveSigB(const std::string& data)
	{
		dataB = data;
		receiveSigBCount++;
	}

	void ReceiveDeleteSelf(int data)
	{
		std::cout << "TestA::ReceiveDeleteSelf started" << std::endl;
		delete this;
		std::cout << "TestA::ReceiveDeleteSelf completed" << std::endl;
	}

	std::string dataB;
};

class TestB : public lsignal::slot
{
public:
	lsignal::signal<void(const std::string& data)> sigB;

	void ReceiveSigA(int data)
	{
		//std::cout << "ReceiveSigA = " << data << std::endl;
		dataA = data;
		receiveSigACount++;
	}

	int dataA;

	lsignal::connection explicitConnectionA;
};


void TestSignalInClass()
{
	TestRunner::StartTest(MethodName);

	int dataA = 1023;
	std::string dataB = "DFBZ12Paql";
	TestA ta;
	TestB tb;

	ta.sigA.connect(&tb, &TestB::ReceiveSigA, &tb);
	tb.sigB.connect(&ta, &TestA::ReceiveSigB, &ta);

	ta.sigA(dataA);
	tb.sigB(dataB);

	AssertHelper::VerifyValue(true, tb.dataA==dataA, "Verify dataA");
	AssertHelper::VerifyValue(true, ta.dataB==dataB, "Verify dataB");
}

void TestSignalDestroyListener()
{
	TestRunner::StartTest(MethodName);
	std::vector<TestB*> listeners;
	TestA ta;
	const size_t count = 1000;
	for(size_t i=0; i<count; i++)
	{
		TestB* pb = new TestB();
		listeners.push_back(pb);
		ta.sigA.connect(pb, &TestB::ReceiveSigA, pb);
	}

	receiveSigACount = 0;
	ta.sigA(23);
	AssertHelper::VerifyValue(true, receiveSigACount==count, "Verify siga count1");	

	for(size_t i=0; i<count; i+=2)
	{
		delete listeners[i];
		listeners[i] = nullptr;
	}

	receiveSigACount = 0;
	ta.sigA(34);
	AssertHelper::VerifyValue(true, receiveSigACount==count/2, "Verify siga count/2");

	receiveSigACount = 0;
	ta.sigA.disconnect_all();
	ta.sigA(45);
	AssertHelper::VerifyValue(true, receiveSigACount==0, "Verify disconnect_all");

	for(TestB* pb : listeners)
		delete pb;
}

void TestDisconnectConnection()
{
	TestRunner::StartTest(MethodName);
	std::vector<TestB*> listeners;
	TestA ta;
	const size_t count = 1000;
	for(size_t i=0; i<count; i++)
	{
		TestB* pb = new TestB();
		listeners.push_back(pb);
		pb->explicitConnectionA = ta.sigA.connect(pb, &TestB::ReceiveSigA, pb);
	}

	receiveSigACount = 0;
	ta.sigA(23);
	AssertHelper::VerifyValue(true, receiveSigACount==count, "Verify siga count1");	

	for(size_t i=0; i<count; i+=2)
	{
		ta.sigA.disconnect(listeners[i]->explicitConnectionA);
	}

	receiveSigACount = 0;
	ta.sigA(34);
	AssertHelper::VerifyValue(true, receiveSigACount==count/2, "Verify siga count/2");

	receiveSigACount = 0;
	for(size_t i=0; i<count; i++)
	{
		ta.sigA.disconnect(listeners[i]->explicitConnectionA);
	}

	ta.sigA(45);
	AssertHelper::VerifyValue(true, receiveSigACount==0, "Verify disconnect_all");

	for(TestB* pb : listeners)
		delete pb;
}

void TestDestroySignal()
{
	TestRunner::StartTest(MethodName);
	std::vector<TestB*> listeners;

	{
		TestA ta;
		const size_t count = 1000;
		for(size_t i=0; i<count; i++)
		{
			TestB* pb = new TestB();
			listeners.push_back(pb);
			ta.sigA.connect(pb, &TestB::ReceiveSigA, pb);
		}

		for(size_t i=0; i<count; i+=2)
		{
			ta.sigA.disconnect(listeners[i]->explicitConnectionA);
		}

		for(size_t i=0; i<count; i+=3)
		{
			delete listeners[i];
			listeners[i] = nullptr;
		}
	}

	for(TestB* pb : listeners)
		delete pb;
}

void TestSignalSelfDelete()
{
	TestRunner::StartTest(MethodName);

	TestA* pa = new TestA();
	pa->sigA.connect(pa, &TestA::ReceiveDeleteSelf, pa);
	pa->sigA(37);
}

//TODO Test copy signal
//TODO Test add connection in callback
//TODO Test remove connection in callback

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	ExecuteTest(CreateSignal_SignalShouldBeUnlocked);
	ExecuteTest(LockSignal_SignalShouldBeLocked);
	ExecuteTest(UnlockSignal_SignalShouldBeUnlocked);
	ExecuteTest(CallSignalWithSingleConnection_SignalShouldBeCalled);
	ExecuteTest(CallSignalWithMultipleConnections_SignalShouldBeCalled);
	ExecuteTest(CallSignalWithoutConnections_SignalShouldBeCalled);
	ExecuteTest(SetSameOwnerToSeveralSignals_AllSignalsShouldBeNotifiedAboutOwnerDestruction);
	ExecuteTest(CreateSignalToSignalConnection_WhenFirstSignalIsDestroyed_SecondSignalShouldBeNotifed);
	ExecuteTest(CreateSignalToSignalConnection_WhenSecondSignalIsDestryoed_FirstSignalShouldBeNotifed);

	ExecuteTest(TestSignalInClass);
	ExecuteTest(TestSignalDestroyListener);
	ExecuteTest(TestDisconnectConnection);
	ExecuteTest(TestDestroySignal);

	ExecuteTest(TestSignalSelfDelete);
	//std::cin.get();

	return 0;
}

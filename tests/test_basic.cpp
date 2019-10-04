#include "tests.h"

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

//Test class signal/slot

static int receiveSigACount;
static int receiveSigBCount;
static bool connectionAddedInCallbackCalled;

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

	void AddConnectionInCallbackA(int data)
	{
		std::cout << "AddConnectionInCallbackA = " << data << std::endl;
		sigA.connect(this, &TestA::ConnectionAddedInCallback, this);
		receiveSigACount++;
	}

	void ConnectionAddedInCallback(int data)
	{
		std::cout << "ConnectionAddedInCallback" << std::endl;
		connectionAddedInCallbackCalled = true;
		receiveSigACount++;
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

	AssertHelper::VerifyValue(true, tb.dataA == dataA, "Verify dataA");
	AssertHelper::VerifyValue(true, ta.dataB == dataB, "Verify dataB");
}

void TestSignalDestroyListener()
{
	TestRunner::StartTest(MethodName);
	std::vector<TestB*> listeners;
	TestA ta;
	const size_t count = 1000;
	for (size_t i = 0; i < count; i++)
	{
		TestB* pb = new TestB();
		listeners.push_back(pb);
		ta.sigA.connect(pb, &TestB::ReceiveSigA, pb);
	}

	receiveSigACount = 0;
	ta.sigA(23);
	AssertHelper::VerifyValue(count, receiveSigACount, "Verify siga count1");

	for (size_t i = 0; i < count; i += 2)
	{
		delete listeners[i];
		listeners[i] = nullptr;
	}

	receiveSigACount = 0;
	ta.sigA(34);
	AssertHelper::VerifyValue(count / 2, receiveSigACount, "Verify siga count/2");

	receiveSigACount = 0;
	ta.sigA.disconnect_all();
	ta.sigA(45);
	AssertHelper::VerifyValue(0, receiveSigACount, "Verify disconnect_all");

	for (TestB* pb : listeners)
		delete pb;
}

void TestDisconnectConnection()
{
	TestRunner::StartTest(MethodName);
	std::vector<TestB*> listeners;
	TestA ta;
	const size_t count = 1000;
	for (size_t i = 0; i < count; i++)
	{
		TestB* pb = new TestB();
		listeners.push_back(pb);
		pb->explicitConnectionA = ta.sigA.connect(pb, &TestB::ReceiveSigA, pb);
	}

	receiveSigACount = 0;
	ta.sigA(23);
	AssertHelper::VerifyValue(true, receiveSigACount == count, "Verify siga count1");

	for (size_t i = 0; i < count; i += 2)
	{
		ta.sigA.disconnect(listeners[i]->explicitConnectionA);
	}

	receiveSigACount = 0;
	ta.sigA(34);
	AssertHelper::VerifyValue(true, receiveSigACount == count / 2, "Verify siga count/2");

	receiveSigACount = 0;
	for (size_t i = 0; i < count; i++)
	{
		ta.sigA.disconnect(listeners[i]->explicitConnectionA);
	}

	ta.sigA(45);
	AssertHelper::VerifyValue(true, receiveSigACount == 0, "Verify disconnect_all");

	for (TestB* pb : listeners)
		delete pb;
}

void TestDestroySignal()
{
	TestRunner::StartTest(MethodName);
	std::vector<TestB*> listeners;

	{
		TestA ta;
		const size_t count = 1000;
		for (size_t i = 0; i < count; i++)
		{
			TestB* pb = new TestB();
			listeners.push_back(pb);
			ta.sigA.connect(pb, &TestB::ReceiveSigA, pb);
		}

		for (size_t i = 0; i < count; i += 2)
		{
			ta.sigA.disconnect(listeners[i]->explicitConnectionA);
		}

		for (size_t i = 0; i < count; i += 3)
		{
			delete listeners[i];
			listeners[i] = nullptr;
		}
	}

	for (TestB* pb : listeners)
		delete pb;
}

void TestSignalSelfDelete()
{
	TestRunner::StartTest(MethodName);

	TestA* pa = new TestA();
	pa->sigA.connect(pa, &TestA::ReceiveDeleteSelf, pa);
	pa->sigA(37);
}

void TestSignalCopy()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void(int, bool)> sg;

	int paramOne = 7;
	bool paramTwo = true;

	bool receiverCalled = false;

	std::function<void(int, bool)> receiver = [=, &receiverCalled](int p0, bool p1)
	{
		receiverCalled = true;

		AssertHelper::VerifyValue(paramOne, p0, "First parameter should be as expected.");
		AssertHelper::VerifyValue(paramTwo, p1, "Second parameter should be as expected.");
	};

	sg.connect(receiver, nullptr);

	lsignal::signal<void(int, bool)> sg2;
	sg2 = sg;
	sg2(paramOne, paramTwo);

	AssertHelper::VerifyValue(true, receiverCalled, "Receiver2 should be called.");

	receiverCalled = false;
	lsignal::signal<void(int, bool)> sg3 = sg;
	sg3(paramOne, paramTwo);

	AssertHelper::VerifyValue(true, receiverCalled, "Receiver3 should be called.");
}

void TestAddConnectionInCallback()
{
	TestRunner::StartTest(MethodName);

	TestA ta;
	connectionAddedInCallbackCalled = false;

	lsignal::connection explicitConnectionA = ta.sigA.connect(&ta, &TestA::AddConnectionInCallbackA, &ta);
	receiveSigACount = 0;
	ta.sigA(12);
	AssertHelper::VerifyValue(false, connectionAddedInCallbackCalled, "connectionAddedInCallbackCalled==false");
	AssertHelper::VerifyValue(1, receiveSigACount, "receiveSigACount==1");
	ta.sigA.disconnect(explicitConnectionA);
	receiveSigACount = 0;
	ta.sigA(22);
	AssertHelper::VerifyValue(1, receiveSigACount, "receiveSigACount==1");
	AssertHelper::VerifyValue(true, connectionAddedInCallbackCalled, "connectionAddedInCallbackCalled==false");
}

void TestRemoveConnectionInCallback()
{
	TestRunner::StartTest(MethodName);
	lsignal::signal<void()> sg;
	bool callled = false;

	lsignal::connection explicitConnection = sg.connect([&callled]()
	{
		callled = true;
	}, nullptr);
	sg();
	AssertHelper::VerifyValue(true, callled, "Connection not called");

	sg.connect([&sg, &explicitConnection]()
	{
		sg.disconnect(explicitConnection);
	}, nullptr);
	sg();
	callled = false;
	sg();
	AssertHelper::VerifyValue(false, callled, "Connection called, but not required it.");
}

void TestAddAndRemoveConnection()
{
	TestRunner::StartTest(MethodName);
	lsignal::signal<void()> sg;
	bool callled = false;

	lsignal::connection explicitConnection = sg.connect([&callled]()
	{
		callled = true;
	}, nullptr);

	sg.disconnect(explicitConnection);
	sg();

	AssertHelper::VerifyValue(false, callled, "Connection called, but not required it.");
}

void TestDestroyOwnerBeforeSignal()
{
	TestRunner::StartTest(MethodName);

	TestB* pb = new TestB();

	lsignal::signal<void(int data)> sigA;
	sigA.connect(pb, &TestB::ReceiveSigA, pb);

	sigA(37);

	delete pb;
}

void TestCallSignalAfterDeleteOwner()
{
	TestRunner::StartTest(MethodName);

	TestB* pb = new TestB();

	lsignal::signal<void(int data)> sigA;
	sigA.connect(pb, &TestB::ReceiveSigA, pb);

	sigA(37);

	delete pb;

	receiveSigACount = 0;
	sigA(42);
	AssertHelper::VerifyValue(0, receiveSigACount, "Connection called, but not required it.");
}

void TestCallSignalAfterDeleteOwner2()
{
	TestRunner::StartTest(MethodName);

	TestB* pb = new TestB();

	lsignal::signal<void(int data)> sigA;
	sigA.connect(pb, &TestB::ReceiveSigA, pb);

	delete pb;

	receiveSigACount = 0;
	sigA(42);
	AssertHelper::VerifyValue(0, receiveSigACount, "Connection called, but not required it.");
}

void TestDeleteOwnerAndDisconnectAll()
{
	TestRunner::StartTest(MethodName);

	TestB* pb = new TestB();

	lsignal::signal<void(int data)> sigA;
	sigA.connect(pb, &TestB::ReceiveSigA, pb);

	delete pb;
	sigA.disconnect_all();
}

void TestDisconnectAllInSignal()
{
	TestRunner::StartTest(MethodName);

	TestA* pa = new TestA();
	TestB* pb = new TestB();
	pa->sigA.connect(pb, &TestB::ReceiveSigA, pb);

	receiveSigACount = 0;
	pa->sigA(37);
	AssertHelper::VerifyValue(1, receiveSigACount, "Verify disconnect_all");
	pa->sigA.connect([pa](int) {pa->sigA.disconnect_all(); }, pa);

	pa->sigA(37);

	receiveSigACount = 0;
	pa->sigA(37);
	AssertHelper::VerifyValue(0, receiveSigACount, "Verify disconnect_all");

	delete pa;
	delete pb;
}

void TestRecursiveSignalCall()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;

	int recursive_count = 5;

	sig.connect([&sig, &recursive_count]()
	{
		recursive_count--;
		if (recursive_count)
			sig();
	}, nullptr);

	sig();
}

void TestRecursiveSignalAddDelete()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void(int)> sig;

	TestB* tb = new TestB;
	sig.connect(tb, &TestB::ReceiveSigA, tb);

	receiveSigACount = 0;
	sig(12);
	AssertHelper::VerifyValue(1, receiveSigACount, "Verify 1");

	const int recursive_count = 7;
	int recursive_index = recursive_count;
	sig.connect([&sig, &recursive_index, &tb](int a)
	{
		recursive_index--;
		if (recursive_index)
			sig(a);
		else
		{
			delete tb;
			tb = nullptr;
		}
	}, nullptr);

	receiveSigACount = 0;
	sig(23);
	AssertHelper::VerifyValue(recursive_count, receiveSigACount, "Verify recursive");

	sig.disconnect_all();

	const int recursive_add = 3;
	recursive_index = recursive_count;
	sig.connect([&sig, &recursive_index, &tb, recursive_add](int a)
	{
		recursive_index--;

		if (recursive_index == recursive_add)
		{
			tb = new TestB;
			sig.connect(tb, &TestB::ReceiveSigA, tb);
		}

		if (recursive_index)
			sig(a);
	}, nullptr);

	receiveSigACount = 0;
	sig(33);

	AssertHelper::VerifyValue(recursive_add, receiveSigACount, "Verify recursive");
}

void TestConnectEmptySignal()
{
	TestRunner::StartTest(MethodName);

	std::function<void()> empty_fn_void;
	lsignal::signal<void()> sig_void;
	sig_void.connect(empty_fn_void, nullptr);
	sig_void();

	std::function<int()> empty_fn_int;
	lsignal::signal<int()> sig_int;
	sig_int.connect(empty_fn_int, nullptr);
	sig_int();
}

void TestConnectionDisconnect()
{
	TestRunner::StartTest(MethodName);
	int called = 0;
	lsignal::signal<void()> sig_void;
	lsignal::connection cn = sig_void.connect([&called]() { called++; }, nullptr);

	sig_void();

	AssertHelper::VerifyValue(1, called, "Called once");

	called = 0;
	cn.disconnect();
	sig_void();

	AssertHelper::VerifyValue(0, called, "Dont call after disconnect");
}

void TestConnectionDisconnectWithOwner()
{
	TestRunner::StartTest(MethodName);
	int called = 0;
	lsignal::signal<void()> sig_void;
	lsignal::slot owner;
	lsignal::connection cn = sig_void.connect([&called]() { called++; }, &owner);

	sig_void();

	AssertHelper::VerifyValue(1, called, "Called once");

	called = 0;
	cn.disconnect();
	sig_void();
	AssertHelper::VerifyValue(0, called, "Dont call after disconnect");

	called = 0;
	cn.disconnect();
	sig_void();
	AssertHelper::VerifyValue(0, called, "Dont call after disconnect");
}

void TestConnectionDisconnectWithOwnerAfterOwnerDelete()
{
	TestRunner::StartTest(MethodName);
	int called = 0;
	lsignal::signal<void()> sig_void;
	lsignal::slot* owner = new lsignal::slot();
	lsignal::connection cn = sig_void.connect([&called]() { called++; }, owner);

	sig_void();

	AssertHelper::VerifyValue(1, called, "Called once");

	delete owner;

	called = 0;
	sig_void();
	AssertHelper::VerifyValue(0, called, "Dont call after disconnect");

	called = 0;
	cn.disconnect();
	sig_void();
	AssertHelper::VerifyValue(0, called, "Dont call after disconnect");
}

void TestConnectionDisconnectWithOwnerAfterSignalDelete()
{
	TestRunner::StartTest(MethodName);
	int called = 0;
	lsignal::signal<void()>* sig_void = new lsignal::signal<void()>();
	lsignal::slot* owner = new lsignal::slot();
	lsignal::connection cn = sig_void->connect([&called]() { called++; }, owner);

	(*sig_void)();
	AssertHelper::VerifyValue(1, called, "Called once");

	delete sig_void;
	cn.disconnect();
	delete owner;
}

void CallBasicTests()
{
	ExecuteTest(CreateSignal_SignalShouldBeUnlocked);
	ExecuteTest(LockSignal_SignalShouldBeLocked);
	ExecuteTest(UnlockSignal_SignalShouldBeUnlocked);
	ExecuteTest(CallSignalWithSingleConnection_SignalShouldBeCalled);
	ExecuteTest(CallSignalWithMultipleConnections_SignalShouldBeCalled);
	ExecuteTest(CallSignalWithoutConnections_SignalShouldBeCalled);
	ExecuteTest(SetSameOwnerToSeveralSignals_AllSignalsShouldBeNotifiedAboutOwnerDestruction);

	ExecuteTest(TestSignalInClass);
	ExecuteTest(TestSignalDestroyListener);
	ExecuteTest(TestDisconnectConnection);
	ExecuteTest(TestDestroySignal);

	ExecuteTest(TestSignalSelfDelete);
	ExecuteTest(TestSignalCopy);
	ExecuteTest(TestAddConnectionInCallback);
	ExecuteTest(TestRemoveConnectionInCallback);
	ExecuteTest(TestAddAndRemoveConnection);

	ExecuteTest(TestDestroyOwnerBeforeSignal);
	ExecuteTest(TestCallSignalAfterDeleteOwner);
	ExecuteTest(TestCallSignalAfterDeleteOwner2);
	ExecuteTest(TestDeleteOwnerAndDisconnectAll);
	ExecuteTest(TestDisconnectAllInSignal);

	ExecuteTest(TestRecursiveSignalCall);
	ExecuteTest(TestRecursiveSignalAddDelete);

	ExecuteTest(TestConnectEmptySignal);

	ExecuteTest(TestConnectionDisconnect);
	ExecuteTest(TestConnectionDisconnectWithOwner);
	ExecuteTest(TestConnectionDisconnectWithOwnerAfterOwnerDelete);
	ExecuteTest(TestConnectionDisconnectWithOwnerAfterSignalDelete);
}
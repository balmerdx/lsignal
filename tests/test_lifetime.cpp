#include "tests.h"

// Tests for signal copy/move semantics, the empty()/deferred-prune behaviour,
// mutating a signal from inside its own callback, exception safety of
// operator(), and lsignal::slot lifetime scenarios not covered by
// CallBasicTests().

//----------------------------------------------------------------------------
// Copy / move

void TestSelfAssignSignal()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	int called = 0;
	sig.connect([&called]() { called++; }, nullptr);

	//Written through a pointer so the compiler can't flag it as a literal
	//"x = x" self-assignment; this must not deadlock (signal::operator=
	//locks both signals' mutexes with std::lock, which is the same mutex
	//twice when this == &rhs).
	lsignal::signal<void()>* p = &sig;
	sig = *p;

	sig();
	VERIFY_EQ(1, called, "self-assignment should not corrupt the signal");
}

void TestCopySignalSharesConnectionState()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sg;
	int called = 0;
	lsignal::connection c = sg.connect([&called]() { called++; }, nullptr);

	lsignal::signal<void()> sg2;
	sg2 = sg;

	//A copied signal shares connection_data (not just the callback) with the
	//original - disconnecting via a connection obtained from the original
	//also silences the copy.
	c.disconnect();
	sg2();
	VERIFY_EQ(0, called, "disconnecting the original connection should also affect the copy");
}

void TestCopySignalDisconnectAllAffectsCopy()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sg;
	int called = 0;
	sg.connect([&called]() { called++; }, nullptr);

	lsignal::signal<void()> sg2 = sg;
	sg.disconnect_all();

	sg2();
	VERIFY_EQ(0, called, "disconnect_all() on the original should also affect the copy");
}

void TestCopySignalCopiesLockFlag()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sg;
	sg.set_lock(true);

	lsignal::signal<void()> sg2 = sg;
	VERIFY_TRUE(sg2.is_locked(), "copy should inherit the locked state of the original");
}

void TestCopySignalIndependentConnections()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sg;
	int called_orig = 0, called_copy = 0;
	sg.connect([&called_orig]() { called_orig++; }, nullptr);

	lsignal::signal<void()> sg2 = sg;
	sg2.connect([&called_copy]() { called_copy++; }, nullptr);

	sg();
	VERIFY_EQ(1, called_orig, "original should call its own callback");
	VERIFY_EQ(0, called_copy, "a connection added only to the copy should not appear on the original");

	sg2();
	VERIFY_EQ(2, called_orig, "the shared connection is invoked from the copy too");
	VERIFY_EQ(1, called_copy, "copy's own connection should be called");
}

void TestMoveSignal()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sg;
	int called = 0;
	sg.connect([&called]() { called++; }, nullptr);

	lsignal::signal<void()> moved(std::move(sg));
	moved();
	VERIFY_EQ(1, called, "move-constructed signal should still call the callback");

	lsignal::signal<void()> moved2;
	moved2 = std::move(moved);
	moved2();
	VERIFY_EQ(2, called, "move-assigned signal should still call the callback");
}

void TestCopyEmptySignal()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sg;
	lsignal::signal<void()> sg2 = sg;

	int called = 0;
	sg2.connect([&called]() { called++; }, nullptr);
	sg2();
	VERIFY_EQ(1, called, "copy of a signal without connections should still accept new connections");
}

//----------------------------------------------------------------------------
// empty() and deferred pruning

void TestEmptyAfterDisconnect()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::connection c = sig.connect([]() {}, nullptr);

	VERIFY_TRUE(!sig.empty(), "signal with a connection should not be empty");

	c.disconnect();
	VERIFY_TRUE(!sig.empty(), "empty() does not account for a disconnected-but-not-yet-pruned connection");

	sig(); //prunes the deleted connection
	VERIFY_TRUE(sig.empty(), "empty() should report true once the next emission has pruned it");
}

void TestEmptyAfterOwnerDestroyed()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::slot* owner = new lsignal::slot();
	sig.connect([]() {}, owner);

	delete owner;
	VERIFY_TRUE(!sig.empty(), "empty() does not account for a connection whose owner just died");

	sig();
	VERIFY_TRUE(sig.empty(), "empty() should report true once the next emission has pruned it");
}

void TestDeferredPruneAfterEmission()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;

	for (int i = 0; i < 100; i++)
	{
		lsignal::connection c = sig.connect([]() {}, nullptr);
		c.disconnect();
	}

	VERIFY_TRUE(!sig.empty(), "100 connect+disconnect cycles without emission should leave the list non-empty");

	sig();
	VERIFY_TRUE(sig.empty(), "a single emission should prune all of them at once");
}

//----------------------------------------------------------------------------
// Mutating a signal from inside its own callback

void TestDisconnectLaterConnectionDuringEmission()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::connection c3;
	int call2 = 0, call3 = 0;

	sig.connect([&c3]() { c3.disconnect(); }, nullptr);
	sig.connect([&call2]() { call2++; }, nullptr);
	c3 = sig.connect([&call3]() { call3++; }, nullptr);

	sig();

	VERIFY_EQ(1, call2, "a connection between the disconnecting callback and the disconnected one should still be called");
	VERIFY_EQ(0, call3, "a connection disconnected by an earlier callback should not be called in the same emission");
}

void TestDestroyOwnerDuringEmission()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::slot* owner = new lsignal::slot();
	int call_after1 = 0, call_after2 = 0;

	sig.connect([owner]() { delete owner; }, nullptr);
	sig.connect([&call_after1]() { call_after1++; }, owner);
	sig.connect([&call_after2]() { call_after2++; }, owner);

	sig();

	VERIFY_EQ(0, call_after1, "callback owned by the just-destroyed slot should be skipped in this emission");
	VERIFY_EQ(0, call_after2, "same for the other callback owned by that slot");
}

void TestDestroySignalDuringEmissionWithRemainingCallbacks()
{
	TestRunner::StartTest(MethodName);

	auto *sig = new lsignal::signal<void()>();
	int call2 = 0, call3 = 0;

	sig->connect([sig]() { delete sig; }, nullptr);
	sig->connect([&call2]() { call2++; }, nullptr);
	sig->connect([&call3]() { call3++; }, nullptr);

	(*sig)();

	VERIFY_EQ(1, call2, "callbacks after the one that deleted the signal should still run (internal_data kept alive)");
	VERIFY_EQ(1, call3, "same for the last callback");
}

void TestExceptionFromCallback()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	int call_after = 0;
	lsignal::connection c1 = sig.connect([]() { throw std::runtime_error("boom"); }, nullptr);
	lsignal::connection c2 = sig.connect([&call_after]() { call_after++; }, nullptr);

	bool threw = false;
	try
	{
		sig();
	}
	catch (const std::runtime_error&)
	{
		threw = true;
	}
	VERIFY_TRUE(threw, "exception from a callback should propagate to the caller");
	VERIFY_EQ(0, call_after, "a callback after the throwing one is skipped in that same emission");

	c1.disconnect();
	call_after = 0;
	sig(); //must work normally - _signal_called_count has to have been restored despite the earlier throw
	VERIFY_EQ(1, call_after, "signal should remain fully functional after an earlier emission threw");

	c2.disconnect();
	sig(); //prunes c2, which only happens if _signal_called_count was correctly restored to 0 above
	VERIFY_TRUE(sig.empty(), "deferred pruning should still work after an earlier emission threw");
}

void TestExceptionFromCallbackRecursive()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	bool throw_once = true;

	lsignal::connection connA = sig.connect([&sig, &throw_once]()
	{
		if (throw_once)
		{
			throw_once = false;
			sig(); //nested emission; the exception below unwinds through two operator() frames
		}
	}, nullptr);

	lsignal::connection connB = sig.connect([]() { throw std::runtime_error("boom"); }, nullptr);

	bool threw = false;
	try
	{
		sig();
	}
	catch (const std::runtime_error&)
	{
		threw = true;
	}
	VERIFY_TRUE(threw, "exception from a nested emission should propagate through both levels");

	connB.disconnect();
	int called = 0;
	lsignal::connection connC = sig.connect([&called]() { called++; }, nullptr);

	sig();
	VERIFY_EQ(1, called, "signal should remain functional after an earlier nested-emission exception");

	connA.disconnect();
	connC.disconnect();
	sig(); //prunes connA/connC, which only happens if _signal_called_count was restored to 0 on both nesting levels
	VERIFY_TRUE(sig.empty(), "deferred pruning should still work after an exception unwound a nested emission");
}

//----------------------------------------------------------------------------
// lsignal::slot

void TestSlotDisconnectExplicit()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::slot owner;
	int called = 0;
	sig.connect([&called]() { called++; }, &owner);

	owner.disconnect();
	sig();
	VERIFY_EQ(0, called, "callback should not be called after slot::disconnect()");

	called = 0;
	sig.connect([&called]() { called++; }, &owner); //reconnect the same still-alive slot
	sig();
	VERIFY_EQ(1, called, "slot should accept new connections after disconnect()");
}

void TestSlotDisconnectTwice()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::slot owner;
	int called = 0;
	sig.connect([&called]() { called++; }, &owner);

	owner.disconnect();
	owner.disconnect(); //idempotent, must not crash

	sig();
	VERIFY_EQ(0, called, "callback should stay disconnected after calling disconnect() twice");
}

void TestSlotWithMultipleSignalsPartialDisconnect()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sigA, sigB, sigC;
	lsignal::slot owner;
	int callA = 0, callB = 0, callC = 0;

	sigA.connect([&callA]() { callA++; }, &owner);
	sigB.connect([&callB]() { callB++; }, &owner);
	sigC.connect([&callC]() { callC++; }, &owner);

	owner.disconnect();

	sigA();
	sigB();
	sigC();
	VERIFY_EQ(0, callA, "sigA should not call its callback after the shared owner disconnects");
	VERIFY_EQ(0, callB, "sigB should not call its callback after the shared owner disconnects");
	VERIFY_EQ(0, callC, "sigC should not call its callback after the shared owner disconnects");
}

void TestSlotOutlivesSignal()
{
	TestRunner::StartTest(MethodName);

	lsignal::slot owner;
	{
		lsignal::signal<void()> sig1;
		lsignal::signal<void()> sig2;
		sig1.connect([]() {}, &owner);
		sig2.connect([]() {}, &owner);
		//sig1 and sig2 are destroyed here while owner is still alive.
	}
	//owner's destructor (below, at end of scope) must not crash even though
	//the signals it was registered with are already gone.
}

void CallLifetimeTests()
{
	ExecuteTest(TestSelfAssignSignal);
	ExecuteTest(TestCopySignalSharesConnectionState);
	ExecuteTest(TestCopySignalDisconnectAllAffectsCopy);
	ExecuteTest(TestCopySignalCopiesLockFlag);
	ExecuteTest(TestCopySignalIndependentConnections);
	ExecuteTest(TestMoveSignal);
	ExecuteTest(TestCopyEmptySignal);

	ExecuteTest(TestEmptyAfterDisconnect);
	ExecuteTest(TestEmptyAfterOwnerDestroyed);
	ExecuteTest(TestDeferredPruneAfterEmission);

	ExecuteTest(TestDisconnectLaterConnectionDuringEmission);
	ExecuteTest(TestDestroyOwnerDuringEmission);
	ExecuteTest(TestDestroySignalDuringEmissionWithRemainingCallbacks);
	ExecuteTest(TestExceptionFromCallback);
	ExecuteTest(TestExceptionFromCallbackRecursive);

	ExecuteTest(TestSlotDisconnectExplicit);
	ExecuteTest(TestSlotDisconnectTwice);
	ExecuteTest(TestSlotWithMultipleSignalsPartialDisconnect);
	ExecuteTest(TestSlotOutlivesSignal);
}

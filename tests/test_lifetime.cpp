#include "tests.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>

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

// Not the same thing TestCopySignalIndependentConnections checks (that test
// only verifies a connection added *after* copying isn't visible on the
// other side). This one checks that a callback connected *before* the copy -
// which both signals now call - has independently-copied captured state, not
// a shared closure object. A prototype that stored the connected callable in
// a shared_ptr<void> instead of deep-copying it on signal copy passed every
// other test but silently shared closures here; found only by a manual check
// (test_debug_information/build/copy_independence_check.cpp), never by a
// unit test, per test_debug_information/README.md.
void TestCopySignalIndependentClosureState()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<int()> sg;
	int state = 1;
	sg.connect([state]() mutable { return state++; }, nullptr);

	lsignal::signal<int()> sg2 = sg;

	int r1 = sg();  //original's own closure copy: 1 -> returns 1, advances to 2
	int r2 = sg2(); //copy's own closure copy should be independent, still starting at 1

	VERIFY_EQ(1, r1, "original's closure should start from its own captured state");
	VERIFY_EQ(1, r2, "copy's closure should have an independent copy of the captured state, not share it with the original");
}

// Same idea as TestCopySignalIndependentClosureState, but with several
// callbacks of different capture sizes/alignments connected to the same
// signal before the copy - a wrong storage offset for one callback would
// most plausibly manifest as reading a *neighboring* callback's state, which
// a single differently-sized callback can't catch.
void TestCopySignalIndependentClosureStateMixedSizes()
{
	TestRunner::StartTest(MethodName);

	struct alignas(32) Aligned32
	{
		int state;
	};

	lsignal::signal<void(std::vector<int>&)> sg;

	int small_state = 100;
	sg.connect([small_state](std::vector<int>& out) mutable { out.push_back(small_state++); }, nullptr);

	std::array<long, 16> medium_state;
	for (std::size_t i = 0; i < medium_state.size(); i++)
		medium_state[i] = static_cast<long>(i);
	sg.connect([medium_state](std::vector<int>& out) mutable
	{
		out.push_back(static_cast<int>(medium_state[0]));
		medium_state[0]++;
	}, nullptr);

	Aligned32 aligned_state{7};
	sg.connect([aligned_state](std::vector<int>& out) mutable
	{
		out.push_back(aligned_state.state);
		aligned_state.state++;
	}, nullptr);

	lsignal::signal<void(std::vector<int>&)> sg2 = sg;

	std::vector<int> r1, r2;
	sg(r1);   //original's own copies: 100, 0, 7 -> each advances by one
	sg2(r2);  //copy's own copies must be independent, starting from the same values

	std::vector<int> expected{100, 0, 7};
	VERIFY_TRUE(r1 == expected, "original callbacks should start from their own captured state");
	VERIFY_TRUE(r2 == expected, "copy's callbacks should have independently-copied state, not share it with the original");

	r1.clear();
	sg(r1);
	VERIFY_TRUE((r1 == std::vector<int>{101, 1, 8}), "original's state should have advanced independently of the copy");
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
// WAR-26570, finding 2: destroying a closure must allow re-entering its signal.

namespace
{
	class OperationDeadline
	{
		std::mutex mutex;
		std::condition_variable finished;
		bool done = false;
		std::thread watchdog;

	public:
		explicit OperationDeadline(const char* testName)
			: watchdog([this, testName]()
			{
				std::unique_lock<std::mutex> lock(mutex);
				if (!finished.wait_for(lock, std::chrono::milliseconds(100), [this]() { return done; }))
				{
					// A blocked operation cannot be unwound or safely detached.
					std::fprintf(stderr, "Test %s failed: operation timed out after 100 ms; possible signal mutex deadlock\n", testName);
					std::fflush(stderr);
					std::_Exit(EXIT_FAILURE);
				}
			})
		{
		}

		~OperationDeadline()
		{
			{
				std::lock_guard<std::mutex> lock(mutex);
				done = true;
			}
			finished.notify_one();
			watchdog.join();
		}
	};

	struct ReenterSignalOnDestroy
	{
		lsignal::signal<void()>& signal;
		int& completedDestructions;

		ReenterSignalOnDestroy(lsignal::signal<void()>& signal, int& completedDestructions)
			: signal(signal), completedDestructions(completedDestructions)
		{
		}

		~ReenterSignalOnDestroy()
		{
			(void)signal.empty(); // Takes the same mutex as prune()/clear().
			++completedDestructions;
		}
	};

	lsignal::connection ConnectReentrantCapture(lsignal::signal<void()>& signal, int& completedDestructions)
	{
		auto object = std::make_shared<ReenterSignalOnDestroy>(signal, completedDestructions);
		return signal.connect([object]() { (void)object; }, nullptr);
		// After returning, only the stored closure owns object.
	}

	struct DestructorAction
	{
		std::function<void()> action;
		explicit DestructorAction(std::function<void()> action) : action(std::move(action)) {}
		~DestructorAction() { action(); }
	};

	lsignal::connection ConnectDestructorAction(lsignal::signal<void()>& signal, std::function<void()> action)
	{
		auto object = std::make_shared<DestructorAction>(std::move(action));
		return signal.connect([object]() { (void)object; }, nullptr);
	}
}

void TestPruneOnEmitDestroysClosureOutsideMutex()
{
	TestRunner::StartTest(MethodName);
	int destroyed = 0;
	lsignal::signal<void()> signal;
	auto connection = ConnectReentrantCapture(signal, destroyed);
	connection.disconnect();
	VERIFY_EQ(0, destroyed, "disconnect should defer closure destruction");

	{
		OperationDeadline deadline(MethodName);
		signal();
	}
	VERIFY_EQ(1, destroyed, "pruning on emit should finish the reentrant destructor exactly once");
}

void TestPruneOnCopyDestroysClosureOutsideMutex()
{
	TestRunner::StartTest(MethodName);
	int destroyed = 0;
	lsignal::signal<void()> source;
	auto connection = ConnectReentrantCapture(source, destroyed);
	connection.disconnect();
	VERIFY_EQ(0, destroyed, "disconnect should defer closure destruction");

	{
		OperationDeadline deadline(MethodName);
		lsignal::signal<void()> copy(source);
	}
	VERIFY_EQ(1, destroyed, "copy construction should finish the source's reentrant destructor exactly once");
}

void TestAssignmentPrunesSourceOutsideMutex()
{
	TestRunner::StartTest(MethodName);
	int destroyed = 0;
	lsignal::signal<void()> source, destination;
	auto connection = ConnectReentrantCapture(source, destroyed);
	connection.disconnect();
	VERIFY_EQ(0, destroyed, "disconnect should defer closure destruction");

	{
		OperationDeadline deadline(MethodName);
		destination = source;
	}
	VERIFY_EQ(1, destroyed, "assignment should finish the source's reentrant destructor exactly once");
}

void TestAssignmentClearsDestinationOutsideMutex()
{
	TestRunner::StartTest(MethodName);
	int destroyed = 0;
	lsignal::signal<void()> source, destination;
	ConnectReentrantCapture(destination, destroyed);
	VERIFY_EQ(0, destroyed, "the destination should still own its live closure");

	{
		OperationDeadline deadline(MethodName);
		destination = source;
	}
	VERIFY_EQ(1, destroyed, "assignment should finish the destination's reentrant destructor exactly once");
}

void TestPrunedDestructorConnectsBeforeEmission()
{
	TestRunner::StartTest(MethodName);
	int destroyed = 0, called = 0;
	lsignal::signal<void()> signal;
	auto connection = ConnectDestructorAction(signal, [&]()
	{
		++destroyed;
		signal.connect([&]() { ++called; }, nullptr);
	});
	connection.disconnect();
	{
		OperationDeadline deadline(MethodName);
		signal();
	}
	VERIFY_EQ(1, destroyed, "the disconnected capture should be destroyed once");
	VERIFY_EQ(1, called, "a destructor's new connection should be included in the outer emission");
}

void TestPrunedDestructorEmitsSignal()
{
	TestRunner::StartTest(MethodName);
	int destroyed = 0, called = 0;
	lsignal::signal<void()> signal;
	auto connection = ConnectDestructorAction(signal, [&]() { signal(); ++destroyed; });
	signal.connect([&]() { ++called; }, nullptr);
	connection.disconnect();
	{
		OperationDeadline deadline(MethodName);
		signal();
	}
	VERIFY_EQ(1, destroyed, "nested emission should finish the capture's destructor once");
	VERIFY_EQ(2, called, "the live callback should run in both nested and outer emissions");
}

void TestPrunedDestructorAssignsSignalBeforeEmission()
{
	TestRunner::StartTest(MethodName);
	int oldCalled = 0, newCalled = 0;
	lsignal::signal<void()> signal, replacement;
	replacement.connect([&]() { ++newCalled; }, nullptr);
	auto connection = ConnectDestructorAction(signal, [&]() { signal = replacement; });
	signal.connect([&]() { ++oldCalled; }, nullptr);
	connection.disconnect();
	{
		OperationDeadline deadline(MethodName);
		signal();
	}
	VERIFY_EQ(0, oldCalled, "emission must not retain a cursor into the replaced callback list");
	VERIFY_EQ(1, newCalled, "emission should use the callbacks installed by the destructor");
}

void TestPrunedDestructorDeletesSignal()
{
	TestRunner::StartTest(MethodName);
	int destroyed = 0, called = 0;
	auto* signal = new lsignal::signal<void()>();
	auto connection = ConnectDestructorAction(*signal, [&]() { delete signal; ++destroyed; });
	signal->connect([&]() { ++called; }, nullptr);
	connection.disconnect();
	{
		OperationDeadline deadline(MethodName);
		(*signal)();
	}
	VERIFY_EQ(1, destroyed, "the pruning destructor should delete the signal once");
	VERIFY_EQ(1, called, "internal data should survive deletion until emission finishes");
}

void TestSignalDestructionReentrantConnectAndEmit()
{
	TestRunner::StartTest(MethodName);
	int destroyed = 0, addedDestroyed = 0, called = 0;
	bool emptyDuringDestruction = false;
	std::weak_ptr<DestructorAction> addedCapture;
	auto* signal = new lsignal::signal<void()>();
	ConnectDestructorAction(*signal, [&]()
	{
		emptyDuringDestruction = signal->empty();
		auto object = std::make_shared<DestructorAction>([&]() { ++addedDestroyed; });
		addedCapture = object;
		signal->connect([object, &called]() { ++called; }, nullptr);
		(*signal)();
		++destroyed;
	});
	{
		OperationDeadline deadline(MethodName);
		delete signal;
	}
	VERIFY_TRUE(emptyDuringDestruction, "callbacks must be detached before destroying their captures");
	VERIFY_EQ(1, destroyed, "the original destructor should finish once");
	VERIFY_EQ(0, called, "a signal being destroyed must not restart emission");
	VERIFY_EQ(1, addedDestroyed, "a callback added during destruction must also release its capture");
	VERIFY_TRUE(addedCapture.expired(), "reentrant connect must not leak the new callback");
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

namespace
{
	// Copy ctor throws for the kThrowAt-th *clone* made across all connected
	// instances sharing one clone_position counter - used below to make one
	// of several connected callbacks throw specifically during a signal
	// copy's clone pass, not during the original connect(). clone_position
	// is deliberately separate from the call counter (operator() below): the
	// test also invokes the signal to verify it survived a failed copy, and
	// that must not perturb which clone throws on a later copy attempt.
	struct ThrowOnNthCopy
	{
		int* call_counter;
		int* clone_position;
		int throw_at;
		bool primed = false; //the very first copy (into the signal's own node) must succeed

		ThrowOnNthCopy(int* calls, int* clones, int at)
			: call_counter(calls), clone_position(clones), throw_at(at) {}
		ThrowOnNthCopy(const ThrowOnNthCopy& rhs)
			: call_counter(rhs.call_counter), clone_position(rhs.clone_position), throw_at(rhs.throw_at)
		{
			if (rhs.primed)
			{
				int idx = (*clone_position)++;
				if (idx == throw_at)
					throw std::runtime_error("clone boom");
			}
			primed = true;
		}
		void operator()() const { (*call_counter)++; }
	};
}

void TestSignalCopyWithThrowingCallableCopyCtor()
{
	TestRunner::StartTest(MethodName);

	constexpr int kCount = 5;
	constexpr int kThrowAt = 2;
	int call_count = 0;
	int clone_position = 0;

	lsignal::signal<void()> sigA;
	for (int i = 0; i < kCount; i++)
		sigA.connect(ThrowOnNthCopy(&call_count, &clone_position, kThrowAt), nullptr);

	bool threw = false;
	try
	{
		lsignal::signal<void()> sigB = sigA;
		(void)sigB;
	}
	catch (const std::runtime_error&)
	{
		threw = true;
	}
	VERIFY_TRUE(threw, "a throwing callable copy ctor during signal copy should propagate");

	//sigA itself must be left intact and fully usable - copy_from only ever
	//clones into the *new* signal's storage, never mutates rhs.
	sigA();
	VERIFY_EQ(kCount, call_count, "source signal should be untouched and still call every connected callback");

	//operator= must also not crash or leak, though (unlike the copy ctor
	//case) it's documented as leaving the destination in a valid-but-
	//unspecified state on failure (signal_base::operator= has no rollback -
	//see bugs.md).
	lsignal::signal<void()> sigC;
	sigC.connect([]() {}, nullptr);
	clone_position = 0; //next copy attempt should throw at the same kThrowAt-th clone again
	bool threw_assign = false;
	try
	{
		sigC = sigA;
	}
	catch (const std::runtime_error&)
	{
		threw_assign = true;
	}
	VERIFY_TRUE(threw_assign, "a throwing callable copy ctor during signal operator= should propagate too");
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

// WAR-26570, finding 4: owner records must not grow with dead subscriptions.
void TestSlotConnectionCount()
{
	TestRunner::StartTest(MethodName);
	lsignal::slot owner;
	const lsignal::slot& view = owner;
	VERIFY_EQ(std::size_t{0}, view.connection_count(), "a new slot has no active records");
	VERIFY_EQ(std::size_t{0}, view.connection_count(false), "a new slot has no stored records");

	lsignal::signal<void()> first, second;
	auto disconnected = first.connect([]() {}, &owner);
	auto locked = first.connect([]() {}, &owner);
	second.connect([]() {}, &owner);
	locked.set_lock(true);
	VERIFY_EQ(std::size_t{3}, view.connection_count(), "locked connections still count as active");
	VERIFY_EQ(view.connection_count(true), view.connection_count(), "active must default to true");
	VERIFY_EQ(std::size_t{3}, view.connection_count(false), "all three records should be stored");

	disconnected.disconnect();
	VERIFY_EQ(std::size_t{2}, view.connection_count(), "individual disconnect removes one active record");
	VERIFY_EQ(std::size_t{3}, view.connection_count(false), "counting must not prune deleted records");
	first.disconnect_all();
	VERIFY_EQ(std::size_t{1}, view.connection_count(true), "disconnect_all affects only its signal's records");
	second.disconnect_all();
	VERIFY_EQ(std::size_t{0}, view.connection_count(), "all records are now marked deleted");
	VERIFY_EQ(std::size_t{3}, view.connection_count(false), "disconnect_all leaves owner records stored");

	owner.disconnect();
	VERIFY_EQ(std::size_t{0}, view.connection_count(), "slot::disconnect clears active records");
	VERIFY_EQ(std::size_t{0}, view.connection_count(false), "slot::disconnect clears every stored record");
}

void TestSlotConnectionCountAfterSignalDestroyed()
{
	TestRunner::StartTest(MethodName);
	lsignal::slot owner;
	{
		lsignal::signal<void()> signal;
		signal.connect([]() {}, &owner);
	}
	VERIFY_EQ(std::size_t{1}, owner.connection_count(), "signal destruction does not set the deleted flag");
	VERIFY_EQ(std::size_t{1}, owner.connection_count(false), "counting must not remove an orphan record");
	owner.disconnect();
	VERIFY_EQ(std::size_t{0}, owner.connection_count(false), "explicit owner disconnect removes the orphan record");
}

namespace
{
	// Allow delayed cleanup (tmp/01 sweeps from 16 records), without requiring
	// its exact threshold or immediate removal. Check both batches, not capacity.
	void VerifyBoundedSlotRecords(const std::array<std::size_t, 2>& counts)
	{
		for (std::size_t count : counts)
		{
			const auto message = "dead subscription records should stay bounded at 64; actual: " + std::to_string(count);
			VERIFY_TRUE(count <= 64, message.c_str());
		}
	}
}

void TestSlotRecordsBoundedAfterSignalDestruction()
{
	TestRunner::StartTest(MethodName);
	lsignal::slot owner;
	std::array<std::size_t, 2> counts;
	for (auto& count : counts)
	{
		for (int i = 0; i < 1000; ++i)
		{
			lsignal::signal<void()> signal;
			signal.connect([]() {}, &owner); // Keep no connection handle alive.
		}
		count = owner.connection_count(false);
	}
	VerifyBoundedSlotRecords(counts);
}

void TestSlotRecordsBoundedAfterConnectionDisconnect()
{
	TestRunner::StartTest(MethodName);
	lsignal::slot owner;
	lsignal::signal<void()> signal;
	std::array<std::size_t, 2> counts;
	for (auto& count : counts)
	{
		for (int i = 0; i < 1000; ++i)
		{
			auto connection = signal.connect([]() {}, &owner);
			connection.disconnect();
			signal();
		}
		VERIFY_EQ(std::size_t{0}, owner.connection_count(), "all subscriptions should be marked deleted");
		count = owner.connection_count(false);
	}
	VerifyBoundedSlotRecords(counts);
}

void TestSlotRecordsBoundedAfterDisconnectAll()
{
	TestRunner::StartTest(MethodName);
	lsignal::slot owner;
	lsignal::signal<void()> signal;
	std::array<std::size_t, 2> counts;
	for (auto& count : counts)
	{
		for (int i = 0; i < 1000; ++i)
		{
			signal.connect([]() {}, &owner);
			signal.disconnect_all();
			signal();
		}
		VERIFY_EQ(std::size_t{0}, owner.connection_count(), "disconnect_all should mark every subscription deleted");
		count = owner.connection_count(false);
	}
	VerifyBoundedSlotRecords(counts);
}

void TestSlotRecordCleanupPreservesLiveSubscriptions()
{
	TestRunner::StartTest(MethodName);
	std::array<int, 8> calls{};
	lsignal::slot owner;
	lsignal::signal<void()> live, copied, churn;
	for (std::size_t i = 0; i < 4; ++i)
		live.connect([&, i]() { ++calls[i]; }, &owner);
	{
		lsignal::signal<void()> original;
		for (std::size_t i = 4; i < calls.size(); ++i)
			original.connect([&, i]() { ++calls[i]; }, &owner);
		copied = original;
	}
	VERIFY_EQ(std::size_t{8}, owner.connection_count(), "a signal copy shares the existing owner records");
	std::array<std::size_t, 2> counts;
	for (std::size_t batch = 0; batch < counts.size(); ++batch)
	{
		for (int i = 0; i < 1000; ++i)
		{
			auto connection = churn.connect([]() {}, &owner);
			connection.disconnect();
			churn();
		}
		counts[batch] = owner.connection_count(false);
		VERIFY_EQ(std::size_t{8}, owner.connection_count(), "cleanup must preserve all eight live owner records");
		live();
		copied();
		for (int called : calls)
			VERIFY_EQ(static_cast<int>(batch + 1), called, "every live callback must survive periodic cleanup");
	}
	owner.disconnect();
	live();
	copied();
	for (int called : calls)
		VERIFY_EQ(2, called, "owner disconnect must still silence live and copied signals");
	VERIFY_EQ(std::size_t{0}, owner.connection_count(false), "owner disconnect must clear all stored records");
	VerifyBoundedSlotRecords(counts);
}

void TestSlotDisconnectResetsCleanupThreshold()
{
	TestRunner::StartTest(MethodName);
	int calls = 0;
	lsignal::slot owner;
	lsignal::signal<void()> signal;
	for (int i = 0; i < 256; ++i)
		signal.connect([&]() { ++calls; }, &owner);
	VERIFY_EQ(std::size_t{256}, owner.connection_count(), "cleanup must retain a large set of live subscriptions");
	signal();
	VERIFY_EQ(256, calls, "all live callbacks should still run");
	owner.disconnect();
	VERIFY_EQ(std::size_t{0}, owner.connection_count(false), "disconnect must clear the owner records");
	std::array<std::size_t, 2> counts;
	for (auto& count : counts)
	{
		for (int i = 0; i < 1000; ++i)
		{
			auto connection = signal.connect([]() {}, &owner);
			connection.disconnect();
			signal();
		}
		count = owner.connection_count(false);
	}
	VERIFY_EQ(256, calls, "the old callbacks must remain disconnected");
	VERIFY_EQ(std::size_t{0}, owner.connection_count(), "churn should leave no active subscriptions");
	VerifyBoundedSlotRecords(counts);
}

void TestSlotCleanupWithLockedConnectionsAndRetainedHandles()
{
	TestRunner::StartTest(MethodName);
	std::array<int, 8> calls{};
	lsignal::slot owner;
	lsignal::signal<void()> live, churn;
	std::vector<lsignal::connection> handles, deletedHandles;
	for (std::size_t i = 0; i < calls.size(); ++i)
	{
		handles.push_back(live.connect([&, i]() { ++calls[i]; }, &owner));
		if (i < 4)
			handles.back().set_lock(true);
	}
	std::array<std::size_t, 2> counts;
	for (std::size_t batch = 0; batch < counts.size(); ++batch)
	{
		for (int i = 0; i < 1000; ++i)
		{
			deletedHandles.push_back(churn.connect([]() {}, &owner));
			churn.disconnect_all(); // Mark deleted without releasing external handles.
			churn();
		}
		counts[batch] = owner.connection_count(false);
		VERIFY_EQ(std::size_t{8}, owner.connection_count(), "locked subscriptions must survive while deleted records are swept");
		live();
		for (std::size_t i = 0; i < calls.size(); ++i)
			VERIFY_EQ(i < 4 ? 0 : static_cast<int>(batch + 1), calls[i], "cleanup must preserve callback lock state");
	}
	handles[4].disconnect();
	for (std::size_t i = 0; i < 4; ++i)
		handles[i].set_lock(false);
	live();
	for (std::size_t i = 0; i < calls.size(); ++i)
		VERIFY_EQ(i < 4 ? 1 : (i == 4 ? 2 : 3), calls[i], "retained handles must still unlock and disconnect after cleanup");
	const auto beforeDisconnect = calls;
	owner.disconnect();
	live();
	VERIFY_TRUE(beforeDisconnect == calls, "owner disconnect must still silence callbacks with retained handles");
	VERIFY_EQ(std::size_t{0}, owner.connection_count(false), "owner disconnect must release all records");
	VerifyBoundedSlotRecords(counts);
}

void TestSlotCleanupPreservesOrphanWithRetainedHandle()
{
	TestRunner::StartTest(MethodName);
	lsignal::slot owner;
	lsignal::connection retained;
	{
		lsignal::signal<void()> signal;
		retained = signal.connect([]() {}, &owner);
	}
	lsignal::signal<void()> churn;
	std::array<std::size_t, 2> counts;
	for (int batch = 0; batch < 2; ++batch)
	{
		for (int i = 0; i < 1000; ++i)
		{
			auto connection = churn.connect([]() {}, &owner);
			connection.disconnect();
			churn();
		}
		counts[batch] = owner.connection_count(false);
		VERIFY_EQ(batch == 0 ? std::size_t{1} : std::size_t{0}, owner.connection_count(),
			"an orphan must remain until the last external handle is released");
		// Release without marking deleted, to exercise the last-owner check.
		retained = lsignal::connection();
	}
	VerifyBoundedSlotRecords(counts);
}

void CallLifetimeTests()
{
	ExecuteTest(TestSelfAssignSignal);
	ExecuteTest(TestCopySignalSharesConnectionState);
	ExecuteTest(TestCopySignalDisconnectAllAffectsCopy);
	ExecuteTest(TestCopySignalCopiesLockFlag);
	ExecuteTest(TestCopySignalIndependentConnections);
	ExecuteTest(TestCopySignalIndependentClosureState);
	ExecuteTest(TestCopySignalIndependentClosureStateMixedSizes);
	ExecuteTest(TestMoveSignal);
	ExecuteTest(TestCopyEmptySignal);

	ExecuteTest(TestEmptyAfterDisconnect);
	ExecuteTest(TestEmptyAfterOwnerDestroyed);
	ExecuteTest(TestDeferredPruneAfterEmission);
	ExecuteTest(TestPruneOnEmitDestroysClosureOutsideMutex);
	ExecuteTest(TestPruneOnCopyDestroysClosureOutsideMutex);
	ExecuteTest(TestAssignmentPrunesSourceOutsideMutex);
	ExecuteTest(TestAssignmentClearsDestinationOutsideMutex);
	ExecuteTest(TestPrunedDestructorConnectsBeforeEmission);
	ExecuteTest(TestPrunedDestructorEmitsSignal);
	ExecuteTest(TestPrunedDestructorAssignsSignalBeforeEmission);
	ExecuteTest(TestPrunedDestructorDeletesSignal);
	ExecuteTest(TestSignalDestructionReentrantConnectAndEmit);

	ExecuteTest(TestDisconnectLaterConnectionDuringEmission);
	ExecuteTest(TestDestroyOwnerDuringEmission);
	ExecuteTest(TestDestroySignalDuringEmissionWithRemainingCallbacks);
	ExecuteTest(TestExceptionFromCallback);
	ExecuteTest(TestExceptionFromCallbackRecursive);
	ExecuteTest(TestSignalCopyWithThrowingCallableCopyCtor);

	ExecuteTest(TestSlotDisconnectExplicit);
	ExecuteTest(TestSlotDisconnectTwice);
	ExecuteTest(TestSlotWithMultipleSignalsPartialDisconnect);
	ExecuteTest(TestSlotOutlivesSignal);
	ExecuteTest(TestSlotConnectionCount);
	ExecuteTest(TestSlotConnectionCountAfterSignalDestroyed);
	ExecuteTest(TestSlotRecordsBoundedAfterSignalDestruction);
	ExecuteTest(TestSlotRecordsBoundedAfterConnectionDisconnect);
	ExecuteTest(TestSlotRecordsBoundedAfterDisconnectAll);
	ExecuteTest(TestSlotRecordCleanupPreservesLiveSubscriptions);
	ExecuteTest(TestSlotDisconnectResetsCleanupThreshold);
	ExecuteTest(TestSlotCleanupWithLockedConnectionsAndRetainedHandles);
	ExecuteTest(TestSlotCleanupPreservesOrphanWithRetainedHandle);
}

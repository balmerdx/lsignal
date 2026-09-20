#include "tests.h"

#include <array>
#include <cstdint>

// Tests for parts of the public API that CallBasicTests() never exercises:
// operator() return value, argument passing semantics, all four connect()
// overloads and connection/signal locking.

// Compile-time pins for special-member behavior that's easy to break
// silently while refactoring lsignal.h's internals (see
// test_debug_information/README.md):
//  - lsignal::slot holds a mutex-guarded cleaner list; it must stay
//    non-copyable/non-movable exactly like it was when that list was a
//    plain std::mutex member (implicitly non-copyable) - a pimpl'd slot
//    could silently become copyable (two slots sharing one impl -> double
//    free) unless the copy/move are explicitly deleted.
//  - lsignal::signal must stay both copyable and movable: it's documented
//    and tested (TestSignalCopy, TestMoveSignal) API, not an accident of
//    implementation.
//  - lsignal::connection must stay copyable (copies share connection_data).
static_assert(!std::is_copy_constructible<lsignal::slot>::value, "lsignal::slot must stay non-copyable");
static_assert(!std::is_move_constructible<lsignal::slot>::value, "lsignal::slot must stay non-movable");
static_assert(std::is_copy_constructible<lsignal::signal<void()>>::value, "lsignal::signal must stay copyable");
static_assert(std::is_move_constructible<lsignal::signal<void()>>::value, "lsignal::signal must stay movable");
static_assert(std::is_copy_constructible<lsignal::connection>::value, "lsignal::connection must stay copyable");

//----------------------------------------------------------------------------
// Helpers

struct CopyCounter
{
	static int copy_count;
	static int move_count;
	int value = 0;

	CopyCounter() = default;
	explicit CopyCounter(int v) : value(v) {}
	CopyCounter(const CopyCounter& other) : value(other.value) { copy_count++; }
	CopyCounter(CopyCounter&& other) noexcept : value(other.value) { move_count++; }
	CopyCounter& operator=(const CopyCounter& other) { value = other.value; copy_count++; return *this; }
	CopyCounter& operator=(CopyCounter&& other) noexcept { value = other.value; move_count++; return *this; }

	static void Reset() { copy_count = 0; move_count = 0; }
};
int CopyCounter::copy_count = 0;
int CopyCounter::move_count = 0;

struct ConstMethodHolder : public lsignal::slot
{
	int value = 42;
	int GetValue() const { return value; }
};

struct BaseWithMethod : public lsignal::slot
{
	int value = 0;
	void SetValue(int v) { value = v; }
};

struct DerivedWithMethod : public BaseWithMethod
{
};

static bool g_free_function_called = false;
static void FreeFunctionCallable()
{
	g_free_function_called = true;
}

struct FunctorCallable
{
	bool& called;
	void operator()() { called = true; }
};

struct PlainReceiver
{
	int value = 0;
	void SetValue(int v) { value = v; }
};

//----------------------------------------------------------------------------
// Return value of operator()

void TestReturnValueLastConnection()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<int()> sig;
	sig.connect([]() { return 1; }, nullptr);
	sig.connect([]() { return 2; }, nullptr);
	sig.connect([]() { return 3; }, nullptr);

	int result = sig();
	VERIFY_EQ(3, result, "result should be from the last connected callback");
}

void TestReturnValueEmptySignal()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<int()> sig_int;
	VERIFY_EQ(0, sig_int(), "int() default value for a signal without connections");

	lsignal::signal<std::string()> sig_str;
	VERIFY_EQ(std::string(), sig_str(), "string() default value for a signal without connections");
}

void TestReturnValueLockedSignal()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<int()> sig;
	int call_count = 0;
	sig.connect([&call_count]() { call_count++; return 42; }, nullptr);

	sig.set_lock(true);
	int result = sig();

	VERIFY_EQ(0, result, "locked signal returns the default value");
	VERIFY_EQ(0, call_count, "locked signal does not call its callback");
}

void TestReturnValueAllDisconnected()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<int()> sig;
	lsignal::connection c1 = sig.connect([]() { return 1; }, nullptr);
	lsignal::connection c2 = sig.connect([]() { return 2; }, nullptr);
	c1.disconnect();
	c2.disconnect();

	//Both connections are already flagged deleted; the very next operator()
	//call prunes them before checking _callbacks.empty(), so this hits the
	//same "no live connections" return path as an always-empty signal.
	int result = sig();
	VERIFY_EQ(0, result, "signal with no live connections returns the default value");
}

void TestReturnValueSkipsLockedConnection()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<int()> sig;
	sig.connect([]() { return 1; }, nullptr);
	lsignal::connection c2 = sig.connect([]() { return 2; }, nullptr);
	c2.set_lock(true);

	int result = sig();
	VERIFY_EQ(1, result, "locked last connection is skipped, result comes from the previous callback");
}

void TestReturnValueSkipsEmptyFunction()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<int()> sig;
	sig.connect([]() { return 7; }, nullptr);

	std::function<int()> empty_fn;
	sig.connect(empty_fn, nullptr);

	int result = sig();
	VERIFY_EQ(7, result, "empty std::function slot is skipped, result comes from the previous callback");
}

void TestCallOrderIsConnectOrder()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	std::vector<int> order;
	for (int i = 0; i < 5; i++)
		sig.connect([&order, i]() { order.push_back(i); }, nullptr);

	sig();

	VERIFY_EQ((size_t)5, order.size(), "all callbacks should be called");
	for (int i = 0; i < 5; i++)
		VERIFY_EQ(i, order[i], "call order should match connect order");
}

//----------------------------------------------------------------------------
// Argument passing

void TestArgumentsPassedToEachConnection()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void(std::string)> sig;
	std::vector<std::string> received;
	for (int i = 0; i < 3; i++)
		sig.connect([&received](std::string s) { received.push_back(s); }, nullptr);

	sig(std::string("hello"));

	VERIFY_EQ((size_t)3, received.size(), "all callbacks should be called");
	for (size_t i = 0; i < received.size(); i++)
		VERIFY_EQ(std::string("hello"), received[i], "every callback should see the full argument, not a moved-from one");
}

void TestReferenceArgument()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void(int&)> sig;
	sig.connect([](int& v) { v++; }, nullptr);
	sig.connect([](int& v) { v *= 10; }, nullptr);

	int value = 1;
	sig(value);

	VERIFY_EQ(20, value, "reference argument should be mutated by both callbacks in connect order");
}

void TestConstReferenceArgument()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void(const CopyCounter&)> sig;
	int seen = 0;
	sig.connect([&seen](const CopyCounter& c) { seen = c.value; }, nullptr);
	sig.connect([&seen](const CopyCounter& c) { seen += c.value; }, nullptr);

	CopyCounter::Reset();
	CopyCounter arg(5);
	sig(arg);

	VERIFY_EQ(0, CopyCounter::copy_count, "const reference argument should not be copied");
	VERIFY_EQ(0, CopyCounter::move_count, "const reference argument should not be moved");
	VERIFY_EQ(10, seen, "both callbacks should observe the value");
}

void TestArgumentCopyCount()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void(CopyCounter)> sig;
	const int connections_count = 2;
	for (int i = 0; i < connections_count; i++)
		sig.connect([](CopyCounter) {}, nullptr);

	CopyCounter::Reset();
	CopyCounter arg(3);
	sig(arg);

	//One copy into operator()'s own by-value parameter, plus one copy into
	//std::function::operator()'s own by-value parameter per callback (this
	//is why arguments stay intact across callbacks after the lvalue-passing
	//fix); then std::function forwards its own already-copied local as an
	//rvalue into the target, i.e. one move per callback.
	VERIFY_EQ(1 + connections_count, CopyCounter::copy_count, "copy count should match connections_count + 1");
	VERIFY_EQ(connections_count, CopyCounter::move_count, "move count should match connections_count");
}

//----------------------------------------------------------------------------
// connect() overloads

void TestConnectConstMemberFunction()
{
	TestRunner::StartTest(MethodName);

	ConstMethodHolder holder;
	lsignal::signal<int()> sig;
	sig.connect(&holder, &ConstMethodHolder::GetValue, &holder);

	VERIFY_EQ(42, sig(), "connect() overload for a const member function should work");
}

void TestConnectBaseClassMemberFunction()
{
	TestRunner::StartTest(MethodName);

	DerivedWithMethod derived;
	lsignal::signal<void(int)> sig;
	//T=DerivedWithMethod, Tfn=BaseWithMethod - the member function pointer
	//belongs to the base class, not to the receiver's own class.
	sig.connect(&derived, &BaseWithMethod::SetValue, &derived);

	sig(99);
	VERIFY_EQ(99, derived.value, "member function inherited from a base class should be callable");
}

void TestConnectFreeFunctionAndFunctor()
{
	TestRunner::StartTest(MethodName);

	g_free_function_called = false;
	bool functor_called = false;

	lsignal::signal<void()> sig;
	sig.connect(FreeFunctionCallable, nullptr);
	sig.connect(FunctorCallable{functor_called}, nullptr);

	sig();

	VERIFY_TRUE(g_free_function_called, "a plain free function should be a valid callback");
	VERIFY_TRUE(functor_called, "a class with operator() should be a valid callback");
}

void TestConnectRvalueCallback()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	bool called = false;
	std::function<void()> receiver = [&called]() { called = true; };

	//connect() is a template on the callable type, so this deduces F=std::function<void()>
	//and moves the whole std::function in - it's itself invocable, so it's erased like
	//any other callable.
	sig.connect(std::move(receiver), nullptr);

	sig();
	VERIFY_TRUE(called, "callback moved into the signal should still be called");
}

//----------------------------------------------------------------------------
// Storage of the connected callable itself: alignment, size, destruction,
// exception safety of construction. None of these depend on lsignal_with_cpp
// specifically merging the node and callable into one allocation - they pin
// down observable behavior any storage scheme (a separate new, or a single
// combined block) must preserve.

template<std::size_t Align>
struct alignas(Align) AlignedCallable
{
	bool* called;
	void operator()() const
	{
		VERIFY_EQ(std::uintptr_t(0), reinterpret_cast<std::uintptr_t>(this) % Align,
			"connected callable's storage must respect its own alignment");
		*called = true;
	}
};

template<std::size_t Align>
void RunAlignedCallableCheck()
{
	lsignal::signal<void()> sig;
	bool called = false;
	sig.connect(AlignedCallable<Align>{&called}, nullptr);
	sig();
	VERIFY_TRUE(called, "aligned callable should be invoked");

	//Also exercise the clone path (signal copy), not just the initial connect.
	bool called_copy = false;
	lsignal::signal<void()> sig2 = sig;
	sig2.connect(AlignedCallable<Align>{&called_copy}, nullptr);
	called = false;
	sig2();
	VERIFY_TRUE(called, "original aligned callback should still fire in the copy");
	VERIFY_TRUE(called_copy, "second aligned callback added to the copy should fire too");
}

void TestConnectOverAlignedCallable()
{
	TestRunner::StartTest(MethodName);

	RunAlignedCallableCheck<16>();  //== __STDCPP_DEFAULT_NEW_ALIGNMENT__ on most platforms
	RunAlignedCallableCheck<32>();  //over-aligned: exercises the aligned-new path, if any
	RunAlignedCallableCheck<64>();
}

void TestConnectLargeCaptureCallable()
{
	TestRunner::StartTest(MethodName);

	constexpr int kSize = 1024;
	std::array<unsigned char, kSize> pattern;
	for (int i = 0; i < kSize; i++)
		pattern[i] = static_cast<unsigned char>(i & 0xFF);

	lsignal::signal<void()> sig;
	std::array<unsigned char, kSize> seen{};
	sig.connect([pattern, &seen]() { seen = pattern; }, nullptr);

	//Connect-disconnect-connect around it to make sure a wrong ctx offset
	//shows up as cross-talk with a neighboring node, not "got lucky".
	lsignal::connection extra = sig.connect([]() {}, nullptr);
	extra.disconnect();
	sig.connect([]() {}, nullptr);

	sig();
	VERIFY_TRUE(seen == pattern, "large captured buffer should survive storage byte-for-byte");

	//Exercise copy_from's cloned block too.
	std::array<unsigned char, kSize> seen_copy{};
	lsignal::signal<void()> sig2;
	sig2.connect([pattern, &seen_copy]() { seen_copy = pattern; }, nullptr);
	lsignal::signal<void()> sig3 = sig2;
	sig3();
	VERIFY_TRUE(seen_copy == pattern, "large captured buffer should survive a signal copy byte-for-byte");
}

struct DestructorCounter
{
	static int live_count;
	int marker = 0;

	DestructorCounter() { live_count++; }
	DestructorCounter(const DestructorCounter&) { live_count++; }
	DestructorCounter(DestructorCounter&&) noexcept { live_count++; }
	~DestructorCounter() { live_count--; }
};
int DestructorCounter::live_count = 0;

void TestConnectCallableWithNonTrivialDestructor()
{
	TestRunner::StartTest(MethodName);

	DestructorCounter::live_count = 0;
	{
		lsignal::signal<void()> sig;
		DestructorCounter counter;
		lsignal::connection c1 = sig.connect([counter]() {}, nullptr);
		//counter itself plus the copy captured by the lambda.
		VERIFY_EQ(2, DestructorCounter::live_count, "captured copy should be alive alongside the local");

		c1.disconnect();
		sig(); //triggers deferred prune, which must run the captured copy's destructor
		VERIFY_EQ(1, DestructorCounter::live_count, "disconnect + prune should destroy the captured copy exactly once");
	}
	VERIFY_EQ(0, DestructorCounter::live_count, "local counter should be destroyed at scope exit");

	DestructorCounter::live_count = 0;
	{
		lsignal::signal<void()> sig;
		DestructorCounter counter;
		sig.connect([counter]() {}, nullptr);
		VERIFY_EQ(2, DestructorCounter::live_count, "captured copy alive before signal destruction");
	}
	VERIFY_EQ(0, DestructorCounter::live_count, "destroying the signal should destroy every captured callable exactly once");

	DestructorCounter::live_count = 0;
	{
		lsignal::signal<void()> sigA;
		DestructorCounter counter;
		sigA.connect([counter]() {}, nullptr);
		{
			lsignal::signal<void()> sigB = sigA; //clones the captured copy
			VERIFY_EQ(3, DestructorCounter::live_count, "local + sigA's copy + sigB's cloned copy");
		}
		VERIFY_EQ(2, DestructorCounter::live_count, "destroying sigB should destroy only its own cloned copy");
	}
	VERIFY_EQ(0, DestructorCounter::live_count, "destroying sigA should destroy its copy too");

	DestructorCounter::live_count = 0;
	{
		lsignal::signal<void()> sigA;
		lsignal::signal<void()> sigB;
		DestructorCounter counter;
		sigA.connect([counter]() {}, nullptr);
		sigB.connect([counter]() {}, nullptr);
		VERIFY_EQ(3, DestructorCounter::live_count, "local + sigA's copy + sigB's own copy");

		sigB = sigA; //sigB::operator= must destroy its own old callback before cloning sigA's
		VERIFY_EQ(3, DestructorCounter::live_count, "sigB's old copy destroyed, sigA's copy cloned into sigB");
	}
	VERIFY_EQ(0, DestructorCounter::live_count, "destroying both signals should destroy both remaining copies");
}

struct ThrowingCopyCallable
{
	bool* should_throw;
	ThrowingCopyCallable(bool* flag) : should_throw(flag) {}
	ThrowingCopyCallable(const ThrowingCopyCallable& rhs) : should_throw(rhs.should_throw)
	{
		if (*should_throw)
			throw std::runtime_error("copy ctor boom");
	}
	void operator()() const {}
};

void TestConnectThrowingCallableConstructor()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	bool should_throw = true;
	ThrowingCopyCallable thrower(&should_throw);

	bool threw = false;
	try
	{
		sig.connect(thrower, nullptr); //copies thrower; the copy ctor throws
	}
	catch (const std::runtime_error&)
	{
		threw = true;
	}
	VERIFY_TRUE(threw, "a throwing copy constructor during connect() should propagate");
	VERIFY_TRUE(sig.empty(), "a connect() that threw during construction must not leave a partial entry");

	//Signal must remain fully usable afterwards.
	should_throw = false;
	int called = 0;
	sig.connect([&called]() { called++; }, nullptr);
	sig();
	VERIFY_EQ(1, called, "signal should still work normally after a failed connect()");
}

void TestConnectOwnerDifferentFromReceiver()
{
	TestRunner::StartTest(MethodName);

	//The receiver object does not have to derive from lsignal::slot at all -
	//it is only the owner argument that must be an actual slot*.
	PlainReceiver receiver;
	lsignal::slot* owner = new lsignal::slot();

	lsignal::signal<void(int)> sig;
	sig.connect(&receiver, &PlainReceiver::SetValue, owner);

	sig(5);
	VERIFY_EQ(5, receiver.value, "receiver should be called while owner is alive");

	delete owner;
	sig(10);
	VERIFY_EQ(5, receiver.value, "receiver should not be called after its (separate) owner is destroyed");
}

//----------------------------------------------------------------------------
// Locking

void TestConnectionSetLock()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	int call1 = 0, call2 = 0;
	lsignal::connection c1 = sig.connect([&call1]() { call1++; }, nullptr);
	sig.connect([&call2]() { call2++; }, nullptr);

	VERIFY_TRUE(!c1.is_locked(), "connection should start unlocked");

	c1.set_lock(true);
	VERIFY_TRUE(c1.is_locked(), "is_locked() should reflect set_lock(true)");

	sig();
	VERIFY_EQ(0, call1, "locked connection should not be called");
	VERIFY_EQ(1, call2, "unlocked connection should still be called");

	c1.set_lock(false);
	sig();
	VERIFY_EQ(1, call1, "connection should be called again after unlocking");
	VERIFY_EQ(2, call2, "second connection should be called again");
}

void TestConnectionLockDuringEmission()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::connection c2;
	int call2 = 0;
	sig.connect([&c2]() { c2.set_lock(true); }, nullptr);
	c2 = sig.connect([&call2]() { call2++; }, nullptr);

	sig();
	VERIFY_EQ(0, call2, "connection locked by an earlier callback should be skipped within the same emission");
}

void TestSignalLockDuringEmission()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	int call1 = 0, call2 = 0;
	sig.connect([&sig, &call1]() { call1++; sig.set_lock(true); }, nullptr);
	sig.connect([&call2]() { call2++; }, nullptr);

	sig();
	VERIFY_EQ(1, call1, "first callback should be called");
	VERIFY_EQ(1, call2, "second callback should still be called - lock is only checked at emission entry");

	call1 = 0;
	call2 = 0;
	sig();
	VERIFY_EQ(0, call1, "locked signal should call nothing on the next emission");
	VERIFY_EQ(0, call2, "locked signal should call nothing on the next emission");
}

void TestDefaultConstructedConnection()
{
	TestRunner::StartTest(MethodName);

	lsignal::connection c;
	VERIFY_TRUE(!c.is_locked(), "default constructed connection should report unlocked");

	c.set_lock(true); //should be a safe no-op, not a null-pointer dereference
	VERIFY_TRUE(!c.is_locked(), "set_lock() on an empty connection should stay a no-op");

	c.disconnect(); //should also be a safe no-op
}

void TestConnectionCopyShareState()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	int called = 0;
	lsignal::connection c1 = sig.connect([&called]() { called++; }, nullptr);
	lsignal::connection c2 = c1; //copy: shares the same connection_data

	c2.set_lock(true);
	VERIFY_TRUE(c1.is_locked(), "lock state should be shared between copies of a connection");

	sig();
	VERIFY_EQ(0, called, "locking through the copy should prevent the call");

	c1.disconnect();
	sig();
	VERIFY_EQ(0, called, "disconnecting through the original should disconnect the shared connection");
}

// connection has explicit move ctor/assign only in the lsignal_with_cpp
// variant (previously, and still in lsignal_header_only, a user-declared
// virtual destructor suppressed the implicit move, so `connection c2 =
// std::move(c1)` silently called the copy ctor instead and left c1 valid -
// see bugs.md). Pins the new, intentional behaviour: moving empties the
// source, and a moved-to connection still shares connection_data exactly
// like a copy would. LSIGNAL_WITH_CPP is defined only for the
// lsignal_test target (see CMakeLists.txt).
#ifdef LSIGNAL_WITH_CPP
void TestConnectionMoveSemantics()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	int called = 0;
	lsignal::connection c1 = sig.connect([&called]() { called++; }, nullptr);

	lsignal::connection c2 = std::move(c1);
	VERIFY_TRUE(!c1.is_locked(), "moved-from connection should be empty (is_locked() on it is a safe no-op)");
	c1.disconnect(); //also a safe no-op on the now-empty source
	c1.set_lock(true); //also a safe no-op

	sig();
	VERIFY_EQ(1, called, "the connection moved into c2 should still be live and call back");

	c2.set_lock(true);
	sig();
	VERIFY_EQ(1, called, "locking through the moved-to connection should prevent further calls");

	lsignal::connection c3;
	c3 = std::move(c2);
	VERIFY_TRUE(!c2.is_locked(), "moved-from connection (via move-assign) should be empty");

	c3.disconnect();
	sig();
	VERIFY_EQ(1, called, "disconnecting through the move-assigned-to connection should disconnect it");
}
#endif // LSIGNAL_WITH_CPP

void TestDisconnectConnectionOfAnotherSignal()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sigA;
	lsignal::signal<void()> sigB;
	int called = 0;
	lsignal::connection connFromA = sigA.connect([&called]() { called++; }, nullptr);

	//Documents current behaviour: signal::disconnect() does not check that
	//the connection actually belongs to this signal - it just disconnects it.
	sigB.disconnect(connFromA);

	sigA();
	VERIFY_EQ(0, called, "a connection disconnected via an unrelated signal is still disconnected");
}

void CallApiTests()
{
	ExecuteTest(TestReturnValueLastConnection);
	ExecuteTest(TestReturnValueEmptySignal);
	ExecuteTest(TestReturnValueLockedSignal);
	ExecuteTest(TestReturnValueAllDisconnected);
	ExecuteTest(TestReturnValueSkipsLockedConnection);
	ExecuteTest(TestReturnValueSkipsEmptyFunction);
	ExecuteTest(TestCallOrderIsConnectOrder);

	ExecuteTest(TestArgumentsPassedToEachConnection);
	ExecuteTest(TestReferenceArgument);
	ExecuteTest(TestConstReferenceArgument);
	ExecuteTest(TestArgumentCopyCount);

	ExecuteTest(TestConnectConstMemberFunction);
	ExecuteTest(TestConnectBaseClassMemberFunction);
	ExecuteTest(TestConnectFreeFunctionAndFunctor);
	ExecuteTest(TestConnectRvalueCallback);
	ExecuteTest(TestConnectOverAlignedCallable);
	ExecuteTest(TestConnectLargeCaptureCallable);
	ExecuteTest(TestConnectCallableWithNonTrivialDestructor);
	ExecuteTest(TestConnectThrowingCallableConstructor);
	ExecuteTest(TestConnectOwnerDifferentFromReceiver);

	ExecuteTest(TestConnectionSetLock);
	ExecuteTest(TestConnectionLockDuringEmission);
	ExecuteTest(TestSignalLockDuringEmission);
	ExecuteTest(TestDefaultConstructedConnection);
	ExecuteTest(TestConnectionCopyShareState);
#ifdef LSIGNAL_WITH_CPP
	ExecuteTest(TestConnectionMoveSemantics);
#endif
	ExecuteTest(TestDisconnectConnectionOfAnotherSignal);
}

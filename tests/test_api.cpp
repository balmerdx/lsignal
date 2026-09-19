#include "tests.h"

// Tests for parts of the public API that CallBasicTests() never exercises:
// operator() return value, argument passing semantics, all four connect()
// overloads and connection/signal locking.

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
	ExecuteTest(TestConnectOwnerDifferentFromReceiver);

	ExecuteTest(TestConnectionSetLock);
	ExecuteTest(TestConnectionLockDuringEmission);
	ExecuteTest(TestSignalLockDuringEmission);
	ExecuteTest(TestDefaultConstructedConnection);
	ExecuteTest(TestConnectionCopyShareState);
	ExecuteTest(TestDisconnectConnectionOfAnotherSignal);
}

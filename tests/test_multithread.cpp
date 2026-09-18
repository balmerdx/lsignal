#include "tests.h"


void TestThreadAddDeleteCall()
{
	TestRunner::StartTest(MethodName);
	std::atomic_bool thread_wait_starting(true);
	std::atomic_bool thread_started(false);
	std::atomic_bool thread_executing(true);

	lsignal::signal<void()> sig;

	std::atomic<int> call0_count(0);
	std::atomic<int> call1_count(0);

	std::thread t1([&thread_wait_starting, &thread_started, &thread_executing, &sig, &call1_count]()
	{
		while (thread_wait_starting);
		thread_started = true;
		while (thread_executing)
		{
			lsignal::slot owner1;

			for(int i=0; i<10; i++)
				sig.connect([&]() { call1_count++;  }, &owner1);

			sig();
		}

	});

	thread_wait_starting = false;
	while (!thread_started);

	std::chrono::high_resolution_clock::time_point t_start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < 10000; i++)
	{
		lsignal::slot owner0;

		for(int i=0; i<5; i++)
			sig.connect([&call0_count]() { call0_count++;  }, &owner0);

		sig();
	}

	thread_executing = false;
	t1.join();

	std::chrono::high_resolution_clock::time_point t_end = std::chrono::high_resolution_clock::now();

	std::cout << "call0_count=" << call0_count << "\n";
	std::cout << "call1_count=" << call1_count << "\n";

	std::cout << "elapsed time = " << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count() << " ms\n";
}

void TestThreadDisconnectConnection()
{
	TestRunner::StartTest(MethodName);
	std::atomic_bool thread_wait_starting(true);
	std::atomic_bool thread_started(false);
	std::atomic_bool thread_executing(true);

	lsignal::signal<void()> sig;

	std::atomic<int> call0_count(0);
	std::atomic<int> call1_count(0);

	std::thread t1([&thread_wait_starting, &thread_started, &thread_executing, &sig, &call1_count]()
	{
		while (thread_wait_starting);
		thread_started = true;
		while (thread_executing)
		{
			lsignal::slot owner1;

			std::vector<lsignal::connection> connections;

			for (int i = 0; i < 10; i++)
				connections.push_back(sig.connect([&]() { call1_count++;  }, &owner1));

			//sig();

			for (lsignal::connection& c : connections)
				c.disconnect();
		}

	});

	thread_wait_starting = false;
	while (!thread_started);

	std::chrono::high_resolution_clock::time_point t_start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < 10000; i++)
	{
		lsignal::slot owner0;

		for (int i = 0; i < 5; i++)
			sig.connect([&call0_count]() { call0_count++;  }, &owner0);

		sig();
	}

	thread_executing = false;
	t1.join();

	std::chrono::high_resolution_clock::time_point t_end = std::chrono::high_resolution_clock::now();

	std::cout << "call0_count=" << call0_count << "\n";
	std::cout << "call1_count=" << call1_count << "\n";

	std::cout << "elapsed time = " << std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count() << " ms\n";
}

// Runs `worker` in a background thread (receives a "keep running" flag) while
// `main_loop` runs on the calling thread, then stops and joins the worker.
static void RunConcurrently(std::function<void(const std::atomic_bool& running)> worker, std::function<void()> main_loop)
{
	std::atomic_bool thread_wait_starting(true);
	std::atomic_bool thread_started(false);
	std::atomic_bool thread_executing(true);

	std::thread t1([&thread_wait_starting, &thread_started, &thread_executing, &worker]()
	{
		while (thread_wait_starting);
		thread_started = true;
		worker(thread_executing);
	});

	thread_wait_starting = false;
	while (!thread_started);

	main_loop();

	thread_executing = false;
	t1.join();
}

// Stress signal::set_lock/connection::set_lock (internal_data::_locked and
// connection_data::locked) from two threads.
void TestThreadSetLock()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::slot owner;
	std::atomic<int> call_count(0);

	lsignal::connection conn = sig.connect([&]() { call_count++; }, &owner);

	RunConcurrently(
		[&](const std::atomic_bool& running)
		{
			while (running)
			{
				sig.set_lock(true);
				conn.set_lock(true);
				sig.set_lock(false);
				conn.set_lock(false);
			}
		},
		[&]()
		{
			for (int i = 0; i < 20000; i++)
				sig();
		});

	std::cout << "call_count=" << call_count << "\n";
}

// Stress signal::disconnect_all racing with connect/emit on another thread.
void TestThreadDisconnectAll()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;

	RunConcurrently(
		[&](const std::atomic_bool& running)
		{
			while (running)
				sig.disconnect_all();
		},
		[&]()
		{
			for (int i = 0; i < 5000; i++)
			{
				lsignal::slot owner;
				for (int j = 0; j < 5; j++)
					sig.connect([]() {}, &owner);
				sig();
			}
		});
}

// signal::signal(const signal&) / operator= unconditionally prune deleted
// connections from the source signal, while operator() only does so when it
// is not itself in the middle of iterating (_signal_called_count == 0).
// Copying a signal from one thread while it is being emitted (and connections
// get disconnected) on another thread exercises that gap.
void TestThreadCopySignal()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::slot owner_main;
	std::atomic<int> call_count(0);

	// Keep a decent number of persistent connections so sig() iterates a
	// non-trivial list, widening the race window.
	for (int i = 0; i < 20; i++)
		sig.connect([&]() { call_count++; }, &owner_main);

	RunConcurrently(
		[&](const std::atomic_bool& running)
		{
			while (running)
			{
				lsignal::slot owner;
				std::vector<lsignal::connection> connections;

				for (int i = 0; i < 5; i++)
					connections.push_back(sig.connect([]() {}, &owner));

				lsignal::signal<void()> copy(sig);
				lsignal::signal<void()> copy2;
				copy2 = sig;

				for (lsignal::connection& c : connections)
					c.disconnect();
			}
		},
		[&]()
		{
			for (int i = 0; i < 20000; i++)
				sig();
		});

	std::cout << "call_count=" << call_count << "\n";
}

// Two signals sharing one slot, connected to from two different threads
// concurrently; slot::_cleaners must stay consistent.
void TestThreadSharedSlot()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig_a;
	lsignal::signal<void()> sig_b;

	for (int i = 0; i < 2000; i++)
	{
		lsignal::slot owner;

		std::thread t1([&sig_a, &owner]()
		{
			for (int j = 0; j < 5; j++)
				sig_a.connect([]() {}, &owner);
		});
		std::thread t2([&sig_b, &owner]()
		{
			for (int j = 0; j < 5; j++)
				sig_b.connect([]() {}, &owner);
		});

		t1.join();
		t2.join();

		// owner destructor below disconnects from both signals
	}
}

// Recursive signal emission (bounded depth) driven from two threads at once,
// stressing internal_data::_signal_called_count.
void TestThreadRecursiveCall()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void(int)> sig;
	lsignal::slot owner;
	std::atomic<int> call_count(0);

	const int max_depth = 5;
	sig.connect([&sig, &call_count](int depth)
	{
		call_count++;
		if (depth < max_depth)
			sig(depth + 1);
	}, &owner);

	RunConcurrently(
		[&](const std::atomic_bool& running)
		{
			while (running)
				sig(0);
		},
		[&]()
		{
			for (int i = 0; i < 5000; i++)
				sig(0);
		});

	std::cout << "call_count=" << call_count << "\n";
}

// signal::empty() racing with connect/disconnect on another thread.
void TestThreadEmpty()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	std::atomic<bool> saw_non_empty(false);

	RunConcurrently(
		[&](const std::atomic_bool& running)
		{
			while (running)
			{
				if (!sig.empty())
					saw_non_empty = true;
			}
		},
		[&]()
		{
			for (int i = 0; i < 20000; i++)
			{
				lsignal::slot owner;
				sig.connect([]() {}, &owner);
			}
		});

	std::cout << "saw_non_empty=" << saw_non_empty << "\n";
}

// Deterministic counterpart to TestThreadSetLock: after the locking thread
// has joined (guaranteeing both flags are left unlocked - see its loop body),
// a fresh emission must call the callback.
void TestThreadSetLockDeterministic()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::slot owner;
	std::atomic<int> call_count(0);
	lsignal::connection conn = sig.connect([&call_count]() { call_count++; }, &owner);

	RunConcurrently(
		[&](const std::atomic_bool& running)
		{
			while (running)
			{
				sig.set_lock(true);
				conn.set_lock(true);
				sig.set_lock(false);
				conn.set_lock(false);
			}
		},
		[&]()
		{
			for (int i = 0; i < 20000; i++)
				sig();
		});

	int before = call_count.load();
	sig();
	VERIFY_EQ(before + 1, call_count.load(), "signal should call its callback once the locking thread has joined");
}

// 4 threads connect to the same signal concurrently; every connect() must
// survive into a single emission afterwards - none may be lost to a race
// in create_connection()/_callbacks.
void TestThreadConcurrentConnect()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::slot owner;
	std::atomic<int> call_count(0);

	const int thread_count = 4;
	const int per_thread = 250;

	std::vector<std::thread> threads;
	for (int t = 0; t < thread_count; t++)
	{
		threads.emplace_back([&sig, &owner, &call_count]()
		{
			for (int i = 0; i < per_thread; i++)
				sig.connect([&call_count]() { call_count++; }, &owner);
		});
	}
	for (auto& th : threads)
		th.join();

	sig();
	VERIFY_EQ(thread_count * per_thread, call_count.load(), "every connect() from every thread should be present in a single emission");
}

// 4 threads repeatedly emit the same signal concurrently; the single
// connected callback must be invoked exactly once per emission, from any
// thread, with no lost or duplicated calls.
void TestThreadConcurrentEmit()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	lsignal::slot owner;
	std::atomic<int> call_count(0);
	sig.connect([&call_count]() { call_count++; }, &owner);

	const int thread_count = 4;
	const int iterations = 5000;

	std::vector<std::thread> threads;
	for (int t = 0; t < thread_count; t++)
	{
		threads.emplace_back([&sig]()
		{
			for (int i = 0; i < iterations; i++)
				sig();
		});
	}
	for (auto& th : threads)
		th.join();

	VERIFY_EQ(thread_count * iterations, call_count.load(), "every emit from every thread should reach the single connected callback exactly once");
}

// The non-void branch of operator() (R != void) is otherwise never exercised
// under concurrency by the rest of this file - every other multithread test
// uses signal<void(...)>.
void TestThreadEmitReturnValue()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<int(int)> sig;
	lsignal::slot owner;
	sig.connect([](int v) { return v; }, &owner);
	sig.connect([](int v) { return v * 2; }, &owner);

	std::atomic<bool> mismatch(false);
	const int thread_count = 4;
	const int iterations = 2000;

	std::vector<std::thread> threads;
	for (int t = 0; t < thread_count; t++)
	{
		threads.emplace_back([&sig, &mismatch]()
		{
			for (int i = 0; i < iterations; i++)
			{
				int result = sig(i);
				if (result != i * 2)
					mismatch = true;
			}
		});
	}
	for (auto& th : threads)
		th.join();

	VERIFY_TRUE(!mismatch.load(), "concurrent emissions of a non-void signal should each return the result of the last connected callback");
}

// A slot is destroyed on one thread while another thread keeps emitting the
// signal it was connected to. This only checks memory-safety (ASan/TSan) -
// whether this particular callback still fires for this particular emission
// is an intentional race, not asserted; the callback itself only touches an
// atomic counter, never the slot object being destroyed.
void TestThreadSlotDestroyDuringEmit()
{
	TestRunner::StartTest(MethodName);

	lsignal::signal<void()> sig;
	std::atomic<int> call_count(0);

	for (int i = 0; i < 2000; i++)
	{
		lsignal::slot* owner = new lsignal::slot();
		sig.connect([&call_count]() { call_count++; }, owner);

		std::thread destroyer([owner]() { delete owner; });
		sig();
		destroyer.join();
	}

	std::cout << "call_count=" << call_count << "\n";
}

void CallMultithreadTests()
{
	ExecuteTest(TestThreadAddDeleteCall);
	ExecuteTest(TestThreadDisconnectConnection);
	ExecuteTest(TestThreadSetLock);
	ExecuteTest(TestThreadDisconnectAll);
	ExecuteTest(TestThreadCopySignal);
	ExecuteTest(TestThreadSharedSlot);
	ExecuteTest(TestThreadRecursiveCall);
	ExecuteTest(TestThreadEmpty);

	ExecuteTest(TestThreadSetLockDeterministic);
	ExecuteTest(TestThreadConcurrentConnect);
	ExecuteTest(TestThreadConcurrentEmit);
	ExecuteTest(TestThreadEmitReturnValue);
	ExecuteTest(TestThreadSlotDestroyDuringEmit);
}

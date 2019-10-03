#include "tests.h"


void TestThreadExample()
{
	TestRunner::StartTest(MethodName);
	std::atomic_bool thread_wait_starting(true);
	std::atomic_bool thread_started(false);
	std::atomic_bool thread_executing(true);

	std::thread t1([&thread_wait_starting, &thread_started, &thread_executing]()
	{
		while (thread_wait_starting);
		std::cout << "Thread sunc0\n";
		thread_started = true;

		int idx = 0;
		while (thread_executing)
		{
			std::cout << "Thread idx=" << idx++ <<"\n";
			//std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

	});

	std::cout << "pt0\n";
	thread_wait_starting = false;
	std::cout << "pt1\n";
	while (!thread_started);
	std::cout << "pt2\n";
	//std::this_thread::sleep_for(std::chrono::milliseconds(1));
	thread_executing = false;

	t1.join();

	std::cout << "join\n";
}

void TestThreadAddDeleteCall()
{
	TestRunner::StartTest(MethodName);
	std::atomic_bool thread_wait_starting(true);
	std::atomic_bool thread_started(false);
	std::atomic_bool thread_executing(true);

	lsignal::signal<void()> sig;

	int call0_count = 0;
	int call1_count = 0;

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

void CallMultithreadTests()
{
	ExecuteTest(TestThreadAddDeleteCall);
}

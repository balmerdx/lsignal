#include <algorithm>
#include <iostream>
#include <numeric>
#include <string>
#include <chrono>

#include <boost/signals2.hpp>

// Not part of the CMake build - compile with -Ilsignal_with_cpp (and link
// lsignal_with_cpp/lsignal.cpp), or -Ilsignal_header_only for the header-only
// form instead (no extra .cpp needed then). See root README.md.
#include "lsignal.h"

using lsignal::connection;
using lsignal::signal;
using lsignal::slot;

void print_sum(int x, int y)
{
	std::cout << "sum(" << x << ", " << y << ") = " << (x + y) << "\n";
}

void print_mul(int x, int y)
{
	std::cout << "mul(" << x << ", " << y << ") = " << (x * y) << "\n";
}

int pow2(int x)
{
	return x * x;
}

int pow3(int x)
{
	return x * x * x;
}

void bar()
{
	std::cout << "function: bar\n";
}

struct baz
{
	void operator() ()
	{
		std::cout << "functor: baz\n";
	}
};

struct qux
{
	void print()
	{
		std::cout << "class member: qux\n";
	}
};

struct demo
	: public lsignal::slot
{
	int value;

	demo(int v) : value(v)
	{
	}
};

// Runs fn() `iterations` times and returns the average wall time per call.
// A single call is too short and too noisy to measure reliably (cache
// effects, branch prediction, OS scheduling jitter can easily dominate the
// signal), so this averages over many calls after a warm-up phase.
template<typename Fn>
std::chrono::duration<double> get_performance(Fn&& fn, std::size_t iterations = 1'000'000)
{
	// Warm up caches/branch predictor before measuring.
	for (std::size_t i = 0; i < iterations / 10; ++i)
		fn();

	const auto start = std::chrono::steady_clock::now();

	for (std::size_t i = 0; i < iterations; ++i)
		fn();

	const auto end = std::chrono::steady_clock::now();

	return (end - start) / static_cast<double>(iterations);
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	// example 1
	std::cout << "example #1:\n";

	signal<void(int, int)> data;

	data.connect(print_sum, nullptr);
	data.connect(print_mul, nullptr);

	data(3, 4);

	// example 2
	std::cout << "\nexample #2:\n";

	signal<int(int)> worker;

	worker.connect(pow2, nullptr);
	worker.connect(pow3, nullptr);

	std::cout << "last slot = " << worker(2) << "\n";

	// example 3
	std::cout << "\nexample #3:\n";

	signal<void()> news;

	connection conn_one = news.connect([]() { std::cout << "news #1\n"; }, nullptr);
	connection conn_two = news.connect([]() { std::cout << "news #2\n"; }, nullptr);
	news.connect([]() { std::cout << "news #3\n"; }, nullptr);

	std::cout << "(all connections)\n";
	news();

	std::cout << "(lock connection one)\n";
	conn_one.set_lock(true);
	news();

	std::cout << "(disconnect connection two)\n";
	conn_two.disconnect();
	news();

	// example 4
	std::cout << "\nexample #4:\n";

	signal<void()> dummy;

	auto foo = []() { std::cout << "lambda: foo\n"; };

	dummy.connect(foo, nullptr);
	dummy.connect(bar, nullptr);

	baz b;
	qux q;

	dummy.connect(b, nullptr);
	dummy.connect(&q, &qux::print, nullptr);

	dummy();

	// example 5
	std::cout << "\nexample #5:\n";

	signal<void()> printer;

	{
		demo dm(42);

		auto print_value = [&dm]() { std::cout << "value = " << dm.value << "\n"; };

		printer.connect(std::move(print_value), &dm);

		printer();
	}

	printer();

    // example 7
	std::cout << "\nexample #7: slot\n";

	signal<void()> sig7;
	{
		slot s;

		sig7.connect(bar, &s);
		sig7.connect([](){ std::cout << "sig7\n"; }, &s);
		sig7();
	}
	sig7();

        // example 8
	std::cout << "\nexample #8: disconnect_all\n";

	signal<void()> sig8;
	sig8.connect(bar, nullptr);
	sig8.connect(bar, nullptr);
	sig8.connect(bar, nullptr);
	sig8.disconnect_all();
	sig8();


	// check performance
	//
	// Both signal types use a real (non-dummy) mutex, since that is how
	// lsignal always operates - it has no dummy-mutex mode. This keeps the
	// comparison apples-to-apples instead of comparing a locked signal
	// against an unlocked one.
	using boost_signal = boost::signals2::signal_type<void(),
		boost::signals2::keywords::mutex_type<boost::signals2::mutex>>::type;

	// A side effect the optimizer can't remove, so the benchmark loop can't
	// be folded away to nothing just because the slots "do nothing".
	volatile int sink = 0;
	auto make_slot = [&sink]() { return [&sink]() { sink = sink + 1; }; };

	auto bench = [&](const char* label, std::size_t slot_count)
	{
		lsignal::signal<void()> ls;
		boost_signal bs;

		for (std::size_t i = 0; i < slot_count; ++i)
		{
			ls.connect(make_slot(), nullptr);
			bs.connect(make_slot());
		}

		auto lsignal_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
			get_performance([&ls]() { ls(); }));
		auto boost_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
			get_performance([&bs]() { bs(); }));

		std::cout << "\n" << label << " (" << slot_count << " slot" << (slot_count == 1 ? "" : "s") << "):\n";
		std::cout << "  lsignal        : " << lsignal_ns.count() << " ns/call\n";
		std::cout << "  boost::signals2: " << boost_ns.count() << " ns/call\n";
	};

	std::cout << "\nperformance comparison (average over many calls):\n";

	bench("emit", 1);
	bench("emit", 10);

	return 0;
}

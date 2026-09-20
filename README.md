# lsignal: C++ signal/slot system.

lsignal (or lightweight signal) is a very little and fast C++ thread-safe implementation of signal and
slot system which is based on C++17 code.

# balmerdx - bugfix and simplify 
- simplify/remove untested functionality
- all functionality is covered by tests
- fix delete self/recursive call and other connect/disconnect management 
- safe multithread

### How to use

lsignal comes in two forms, in their own directories - pick one, they're
interchangeable at the API level:

- **`lsignal_with_cpp/`** (recommended default): add `lsignal.h`,
  `lsignal.cpp` and `lsignal_defines.h` to your project. Far less debug info
  per translation unit (see "Debug info vs boost::signals2" below) and ready
  to be built into its own shared library (`LSIGNAL_DLL` in
  `lsignal_defines.h`), at the cost of a real (small) runtime overhead on hot
  signals and needing a second file.
- **`lsignal_header_only/`**: just `#include "lsignal.h"` - one file, no
  build system changes, no `LSIGNAL_DLL`/shared-library story, and no
  per-callback runtime overhead, at the cost of a large fixed `.debug_*` cost
  per translation unit.

See "lsignal_with_cpp vs lsignal_header_only" at the end of this file for the
full comparison.

### Essential classes

##### signal

This is a template class which holds callbacks and can emit signals with certain
arguments. See examples of declarations:

| Declaration                         | Description                                                            |
|------------------------------------ |------------------------------------------------------------------------|
| `lsignal::signal<void()> s;`        | Signal without parameters, return type - _void_                        |
| `lsignal::signal<int(int,int)> t;`  | Signal with two parameters, return type - _int_                        |
| `lsignal::signal<std::string()> u;` | Signal without parameters, return type - _std::string_                 |

You can connect to signal any callback which looks like callable object but be aware than
signature of callback must be equal signature of corresponding signal:

| Callback                            | Description                                                            |
|-------------------------------------|------------------------------------------------------------------------|
| `s.connect(foo, owner);`            | _foo_ is a common function                                             |
| `s.connect(bar, owner);`            | _bar_ is a lambda function                                             |
| `s.connect(baz, owner);`            | _baz_ is a class with operator()                                       |
| `s.connect(&qx, &qux::func, &qx);`  | _qx_ is a instance of class qux : public lsignal::slot                 |

Result of this function is a instance of class `connection`.

When signal is emitted return value will be the result of executing last connected callback.

Arguments are passed to every connected callback as lvalues (copied, not moved), the same
way `boost::signals2` does it - so a callback that happens to move out of its argument
does not leave later callbacks in the same emission with a moved-from value. As a
consequence, a signal whose signature takes a move-only argument by value
(e.g. `lsignal::signal<void(std::unique_ptr<T>)>`) will fail to compile - this is
inherent to multicasting the same argument to several callbacks, not specific to lsignal.

##### connection

`connection` contains link between signal and callback. Available next operations:

| Method                            | Description                                                            |
|-----------------------------------|------------------------------------------------------------------------|
| `is_locked`                       | Check if connection is locked                                          |
| `set_lock`                        | If connection is locked then callback won't be called                  |
| `disconnect`                      | Remove callback from signal                                            |

Also you can pass `connection` directly to `signal::disconnect` for disconnecting this connection.

##### slot

This class similar to `connection` but is used for owhership policy. Look example:

```cpp
class foo : public lsignal::slot
{
    ...
};
...
foo f;

// disconnect when f was destroyed
s.connect([](){ ... }, &f);
```

### Performance

`main.cpp` benchmarks `signal::operator()` for `lsignal` against `boost::signals2`,
both configured with a real (non-dummy) mutex, since `lsignal` has no dummy-mutex
mode. Each measurement averages 1,000,000 calls (after a warm-up pass) to avoid the
noise of timing a single call. Example results (`clang++-20`, `-O2`, x86-64 Linux):

| Connected slots | lsignal   | boost::signals2 |
|-----------------|-----------|------------------|
| 1               | ~20 ns/call  | ~55 ns/call   |
| 10              | ~54 ns/call  | ~240 ns/call  |

`lsignal` is roughly 3-4x faster than `boost::signals2` for emitting a signal.
Exact numbers vary by compiler, flags and hardware — rerun `main.cpp` to measure on
your own setup.

Two debug-info reworks have traded some of this away (see "Debug info vs
boost::signals2" below for what each one bought):

- Type-erasing each connected callable by hand instead of storing it in a
  `std::function` (always via one heap allocation - no small-object
  optimization the way `std::function` has for tiny captures) cost the
  single-slot case ~15% (~14ns → ~16ns); 10-slot emission was unaffected,
  since it was dominated by list iteration and locking, not per-call dispatch.
- Splitting `lsignal.h` into a header + `lsignal.cpp` (opaque
  `detail::signal_impl`, an intrusive callback list walked through a
  non-template `detail::emit_scope`, hand-rolled refcounting instead of
  `shared_ptr`) cost noticeably more, and this time on the 10-slot case
  specifically: ~34ns → ~54ns (+~55%). The 1-slot case is within measurement
  noise of before. The cause is `emit_scope::next()` becoming a real,
  non-inlinable call crossed once per connected callback instead of once per
  emission, plus two atomic refcount operations per emission instead of one
  `shared_ptr` copy - a cost paid on every list node, not just once per
  `operator()` call. `lsignal` stays several times faster than
  `boost::signals2` either way, but this is a real trade, not a free lunch -
  weigh it against how hot your signals actually are.

### Running the tests

The project ships a CMake-based test suite (`tests/`) built as two
executables against the two directories described in "How to use" -
`lsignal_test` (`lsignal_with_cpp/`) and `lsignal_test_header_only`
(`lsignal_header_only/lsignal.h`) - from the same test sources.

```sh
mkdir build && cd build
cmake ..
cmake --build .
./lsignal_test               # expect "passed 85/85"
./lsignal_test_header_only   # expect "passed 84/84" - one fewer: see
                              # "lsignal_with_cpp vs lsignal_header_only" below
```

A successful run ends with a `passed N/N` summary and exit code 0.

By default the tests are built with AddressSanitizer/UBSan enabled. This is
controlled by the `SANITIZER` cache variable, which accepts `none`, `address`
(default), `thread` or `memory`:

```sh
cmake .. -DSANITIZER=thread   # run under ThreadSanitizer instead
cmake .. -DSANITIZER=none     # plain build, no sanitizer
```

`SANITIZER=memory` requires Clang (`-DCMAKE_CXX_COMPILER=clang++`), since
MemorySanitizer isn't implemented by GCC.

### Debug info vs boost::signals2

`lsignal.h` used to nest all of its bookkeeping (`internal_data`, `joint`,
`std::function<R(Args...)>`) directly inside the `signal<R(Args...)>` template,
so the compiler re-emitted debug info for all of it for every distinct signal
signature, and again in every translation unit that used it. Two reworks fixed
that, in two different dimensions - see `test_debug_information/README.md` for
the full methodology, the reasoning behind specific design choices (`std::list`,
not `std::vector`; hand-rolled refcounting, not `shared_ptr`), and everything
below in more detail:

1. **Per-signature/per-connect cost.** All the bookkeeping that doesn't
   depend on `R`/`Args` (the mutex, the lock flag, the callback list) moved
   into a non-template `lsignal::detail::signal_base`/`detail::signal_impl`
   shared by every signature, and each connected callable is type-erased by
   hand instead of going through `std::function`.
2. **Fixed per-TU cost.** `lsignal.h` was header-only, so even a TU that
   never instantiated a single `signal<...>` still paid for every `inline`
   function body the header defined (`std::mutex`, `std::shared_ptr`,
   `std::list`, `std::vector`, `std::atomic` machinery). Splitting the
   library into a header + `lsignal.cpp` (`lsignal_with_cpp/`, see
   "How to use" above) moved essentially all of that into one place compiled
   once per program instead of once per TU - the header now only needs
   `<type_traits>`/`<utility>`. `lsignal_header_only/lsignal.h` keeps the
   original single-file form (rework 1 only) for projects where that fixed
   cost matters less than staying header-only.

Measured the same way as `boost::signals2` (`clang++-20`, `-std=c++20`,
DWARF 5, sum of `.debug_*` section bytes in the `.o`, marginal cost between
16 and 32 identical-shape instances to cancel out one-time fixed costs like
`.debug_abbrev` population - see that file's "Метод" section for why):

Bytes of DWARF debug info per additional instance:

| | `lsignal_with_cpp/lsignal.h` | original `lsignal.h` (before either rework) | `boost::signals2` |
|---|---:|---:|---:|
| new `signal<R(Args...)>` signature | **773** | 49 684 (64×) | 274 643 (355×) |
| new `connect(lambda, owner)` callable type | **2 576** | 5 578 (2.2×) | 10 997 (4.3×) |
| new `connect(&obj, &T::method, owner)` pair | **2 288** | 6 053 (2.6×) | — |

Fixed cost of `#include "lsignal.h"` in a TU that uses none of the above
(N=0 signatures) - this is what the per-TU split specifically targets:

| | `lsignal_with_cpp/lsignal.h` | `lsignal_header_only/lsignal.h` (rework 1 only) |
|---:|---:|---:|
| per TU | **682 bytes** | 73 581 bytes (108×) |
| `lsignal.o` (once per program, not per TU) | 75 034 bytes | — (no separate .cpp) |

682 bytes is close to the floor: an empty translation unit already costs 495
bytes of `.debug_*` on its own (the compilation-unit DIE, `producer` string,
etc.), and `<type_traits>`/`<utility>` with nothing from them actually used
cost nothing measurable on top of that.

Duplication across translation units (8 shared signatures × M identical `.cpp`,
summed `.debug_*` across all `.o`; `lsignal.o` counted once per program, not
once per TU, in the "current" column):

| M (TUs) | `lsignal_with_cpp/lsignal.h` | original `lsignal.h` (before either rework) |
|---:|---:|---:|
| 1 | 82 370 | 483 538 |
| 2 | 89 706 | 967 076 |
| 4 | 104 378 | 1 934 152 |
| 8 | **133 722** | 3 868 304 |

**9.1x** less at M=8, and - unlike the per-signature rework alone, where a
single TU was still worse than before until the fixed `lsignal.o` cost
amortized across enough TUs - already smaller at M=1, because that fixed cost
dropped along with everything else. `extern template class lsignal::signal<Sig>;`
used to cut the per-TU cost further (6.4x, see `test_debug_information/README.md`
§2); with `signal_base` now absorbing nearly everything that used to be
instantiated per signature, it barely moves the number anymore (133 722 vs
122 898 at M=8) and is no longer worth the maintenance cost of the manual
annotation.

Net effect for a project with, say, 200 distinct `signal<...>` signatures
spread across a typical number of TUs: on the order of tens of MB less
`.debug_*` across the build, for the runtime cost documented under
"Performance" above - which, for the per-TU split specifically, is not small
on the 10-slot case. Weigh that trade for your own project rather than taking
it as a strict win.

### lsignal_with_cpp vs lsignal_header_only

Both are built and tested by the same CMake project (`lsignal_test` against
`lsignal_with_cpp/`, `lsignal_test_header_only` against
`lsignal_header_only/lsignal.h`) - neither is a stale or unmaintained copy.
They differ in exactly one observable API way: `lsignal_with_cpp`'s
`connection` has real move semantics (moving empties the source); in
`lsignal_header_only`, as in every lsignal release before this split, a
user-declared virtual destructor suppresses the implicit move, so
`std::move(c)` silently copies instead and the source stays valid (see
`bugs.md`). Everything else - the public API, the tests, the threading
guarantees - is identical.

**`lsignal_with_cpp/` (`lsignal.h` + `lsignal.cpp` + `lsignal_defines.h`)**

- \+ Roughly 9x less `.debug_*` per translation unit for a project with many
  signal signatures spread across many `.cpp` files (see the tables above) -
  the entire point of the split.
- \+ Ready to be built as its own shared library: `LSIGNAL_DLL` in
  `lsignal_defines.h` marks the classes (`connection`, `slot`,
  `detail::signal_base`, `detail::emit_scope`) whose non-inline
  methods/vtable would need to cross a DLL import/export boundary. It's an
  empty macro here (everything is compiled straight into the consumer, no
  boundary exists) - a project that does build lsignal as a `.dll`/`.so`
  defines it to `__declspec(dllexport)`/`dllimport` as appropriate. Building
  or testing an actual shared library isn't covered by this repository.
- \- ~55% slower emission with 10 connected slots (see "Performance" above) -
  `detail::emit_scope::next()` is a real, non-inlinable call now, and every
  emission does two atomic refcount operations instead of one `shared_ptr`
  copy. Not free.
- \- Two files (three, counting `lsignal_defines.h`) instead of one; the
  build needs `lsignal.cpp` compiled and linked in.

**`lsignal_header_only/lsignal.h`**

- \+ True single-file drop-in: `#include` it and nothing else changes about
  your build.
- \+ No `detail::emit_scope` indirection or extra atomic refcount ops - the
  emission numbers under "Performance" above are the ones from before either
  debug-info rework.
- \+ Simpler mental model: no `LSIGNAL_DLL`/shared-library story to think
  about at all, since header-only code is always compiled straight into
  whichever binary uses it.
- \- Every translation unit that includes it pays a large fixed `.debug_*`
  cost (73 581 bytes here) whether or not it ever instantiates a
  `signal<...>` - the cost this whole split exists to avoid.

If you're not sure which to pick: start with `lsignal_with_cpp/`. Reach for
`lsignal_header_only/` only if adding a second file to your build is
genuinely impractical, or if you've profiled a very hot signal (thousands of
emissions per frame, many connected slots) and the ~55% 10-slot regression
shows up as a real cost in your workload.

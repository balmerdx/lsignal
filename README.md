# lsignal: C++ signal/slot system.

lsignal (or lightweight signal) is a very little and fast C++ thread-safe implementation of signal and
slot system which is based on C++17 code.

# balmerdx - bugfix and simplify 
- simplify/remove untested functionality
- all functionality is covered by tests
- fix delete self/recursive call and other connect/disconnect management 
- safe multithread

### How to use

Include `lsignal.h` in your project.

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
noise of timing a single call. Example results (GCC 11, `-O2`, x86-64 Linux):

| Connected slots | lsignal   | boost::signals2 |
|-----------------|-----------|------------------|
| 1               | ~16 ns/call  | ~48 ns/call   |
| 10              | ~33 ns/call  | ~222 ns/call  |

`lsignal` is roughly 3-7x faster than `boost::signals2` for emitting a signal.
Exact numbers vary by compiler, flags and hardware — rerun `main.cpp` to measure on
your own setup.

Since the debug-info rework below (each connected callable is now type-erased by
hand instead of stored in a `std::function`, always via one heap allocation - no
small-object optimization the way `std::function` has for tiny captures),
the single-slot case got ~15% slower (~14ns → ~16ns) than before that change;
10-slot emission is unaffected within measurement noise, since it's dominated
by list iteration and locking, not by per-call dispatch. `lsignal` stays several
times faster than `boost::signals2` either way.

### Running the tests

The project ships a CMake-based test suite (`tests/`) built as the `lsignal_test`
executable.

```sh
mkdir build && cd build
cmake ..
cmake --build .
./lsignal_test
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
signature, and again in every translation unit that used it. It's now split into
a non-template `lsignal::detail::signal_impl` (mutex, lock flag, callback list)
shared by every signature, plus a small hand-rolled type erasure for each
connected callable instead of `std::function`. See `test_debug_information/README.md`
for the full methodology and the reasoning behind the specific design choices
(`std::list`, not `std::vector`; `unique_ptr`, not `shared_ptr`, for the erased
callable). Below is where that leaves `lsignal.h` next to `boost::signals2`,
measured the same way (`clang++-20`, `-std=c++20`, DWARF 5, sum of `.debug_*`
section bytes in the `.o`, taken as the marginal cost between 16 and 32
identical-shape instances to cancel out one-time fixed costs like `.debug_abbrev`
population - see that file's "Метод" section for why).

Bytes of DWARF debug info per additional instance:

| | `lsignal.h` (current) | `lsignal.h` (before this rework) | `boost::signals2` |
|---|---:|---:|---:|
| new `signal<R(Args...)>` signature | **2 005** | 49 684 (24.8×) | 274 643 (137×) |
| new `connect(lambda, owner)` callable type | **2 836** | 5 578 (2.0×) | 10 997 (3.9×) |
| new `connect(&obj, &T::method, owner)` pair | **2 593** | 6 053 (2.3×) | — |

Duplication across translation units (8 shared signatures × M identical `.cpp`,
summed `.debug_*` across all `.o`):

| M (TUs) | `lsignal.h` (current) | `lsignal.h` (before) |
|---:|---:|---:|
| 1 | 152 601 | 483 538 |
| 2 | 305 202 | 967 076 |
| 4 | 610 404 | 1 934 152 |
| 8 | 1 220 808 | 3 868 304 |

Same linear-in-M growth as before (the compiler still re-emits the whole thing
per TU either way - that's what `extern template` is for, see
`test_debug_information/README.md` §2), just from a ~3.2x smaller base unit.

Net effect for a project with, say, 200 distinct `signal<...>` signatures:
roughly 9.7 MB less `.debug_*` per `.o` that uses them, before any
TU-duplication multiplier, for a small (~15%, single-slot only) runtime cost -
see "Performance" above.


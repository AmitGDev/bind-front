# bind_front

A tiny, header-only C++23 utility that binds leading arguments to a callable, like `std::bind_front`, and **propagates `noexcept` per call** from the underlying invocation, independent of the standard library in use.

```cpp
#include "bind_front.hpp"

scheduler.ScheduleTimer(timer_id, 1000,
                        amitgdev::bind_front(&Model::OnTimer, this),
                        std::uint8_t{5});
```

## Why it exists

APIs that run callbacks on a worker thread often require the callback to be `noexcept`, so that an escaping exception cannot silently kill the thread. A common way to enforce that is a constraint such as:

```cpp
template <typename Callback, typename... Args>
concept NothrowTimerCallback =
    std::is_nothrow_invocable_v<Callback, std::uint64_t, Args...>;
```

`std::bind_front` is the obvious tool for binding `this` and a member function, but the standard does not require its wrapper to propagate `noexcept` from the target. Whether it does is a library quality-of-implementation detail. In practice the same code compiled on Windows (MSVC STL) and failed the constraint on Linux, which makes code that depends on it non-portable.

`amitgdev::bind_front` states the behavior explicitly instead of hoping the standard library provides it:

- Each call operator is `noexcept` exactly when the underlying `std::invoke` is nothrow, evaluated with the same cv/ref qualification the stored objects are actually accessed with.
- A call that would be invalid is constrained out of the overload set, so `std::is_invocable` and `std::is_nothrow_invocable` report the correct result on every toolchain, and an invalid call is not a hard error inside the wrapper body.

The wrapper never adds a `noexcept` the target does not have, so a throwing target is never hidden behind `std::terminate`. It simply fails a `noexcept` constraint such as the one above, at the API that imposes it.

## Usage

The header is self-contained: copy `src/bind_front.hpp` into your project and include it. Call it qualified (`amitgdev::bind_front`) to avoid ambiguity with `std::bind_front` found through ADL.

```cpp
#include <cstdint>

#include "bind_front.hpp"

int Subtract(int lhs, int rhs) noexcept { return lhs - rhs; }

struct Model {
  void OnTimer(std::uint64_t timer_id, std::uint8_t count_down) noexcept;
};

void Example(Model& model) {
  // Free function: bound arguments come first.
  auto minus_ten = amitgdev::bind_front(&Subtract, 10);
  minus_ten(3);  // Subtract(10, 3) == 7

  // Member function bound to an object pointer.
  auto on_timer = amitgdev::bind_front(&Model::OnTimer, &model);
  on_timer(1, 5);  // model.OnTimer(1, 5)
}
```

Anything accepted by `std::invoke` works as the target: function pointers, member function pointers, data member pointers, lambdas, and functors, including move-only ones.

### Throwing targets

A throwing target produces a wrapper whose call operator is not `noexcept`. It is rejected by whatever constraint requires `noexcept`, not by the wrapper itself:

```cpp
auto bound = amitgdev::bind_front([](int) {}, 1);  // lambda is not noexcept
static_assert(std::is_invocable_v<decltype(bound)&>);
static_assert(!std::is_nothrow_invocable_v<decltype(bound)&>);
```

Mark the target `noexcept` to get a `noexcept` wrapper:

```cpp
auto bound = amitgdev::bind_front([](int) noexcept {}, 1);
static_assert(std::is_nothrow_invocable_v<decltype(bound)&>);
```

## Behavior

The model for every call is:

| Underlying invocation | Result |
|---|---|
| valid and nothrow | call operator exists and is `noexcept` |
| valid and potentially throwing | call operator exists and is not `noexcept` |
| invalid | call operator is not viable (`std::is_invocable` is `false`) |

This is decided per wrapper category (`&`, `const&`, `&&`, `const&&`) and per argument list, so one wrapper object can be callable as an lvalue and non-viable as an rvalue.

| Aspect | Behavior |
|---|---|
| Target and bound arguments | Stored by value (decayed). Lvalues are copied, rvalues are moved. |
| Reference semantics | Opt in with `std::ref` / `std::cref`. |
| Call | `std::invoke(std::forward_like<Self>(target), std::forward_like<Self>(bound)..., call_args...)`. Call-time arguments keep their value category. |
| Value category of stored state | Follows the wrapper itself. An lvalue wrapper passes the target and bound arguments as lvalues, so it can be invoked repeatedly. An rvalue wrapper passes them as rvalues, so a move-only bound argument can be consumed by value. |
| Constness | Follows the wrapper itself. A `const` wrapper passes `const` target and bound arguments, and is callable only if that invocation is valid. |
| `noexcept` | `std::is_nothrow_invocable_v` of the exact invocation above, per wrapper category. |
| Return type | Preserved exactly (`decltype(auto)`), including references, `void`, and move-only values. |
| `constexpr` | The factory is `constexpr`. The wrapper can be used in constant expressions when the target can. |
| State | The wrapper owns one persistent copy of the target, so a stateful functor keeps its state across calls. |
| Cost | One copy (lvalue target) or one move (rvalue target) when the wrapper is created. Invocation copies nothing. |

### Relation to `std::bind_front`

The call semantics are meant to match `std::bind_front`. What this utility adds is a guarantee that `noexcept` propagation and call detection behave as described above on every standard library. It does not reproduce every constraint `std::bind_front` places on the factory itself (for example, there is no separate constructibility check, so a non-copyable argument fails at the capture). If you do not need the cross-STL guarantee, `std::bind_front` is the simpler choice.

### Pitfalls

- The wrapper does not extend lifetimes. Binding `this` or a raw pointer requires the object to outlive every pending call.
- If the target is declared `noexcept` and still terminates, that is the target's responsibility. The wrapper only guarantees it adds no throwing path of its own.
- `std::ref(callable)` as the **target** is deliberately not covered: whether `std::reference_wrapper::operator()` is `noexcept` is the same kind of library variance this utility exists to avoid. Bind a lambda instead.
- An unqualified `bind_front(...)` call can be ambiguous with `std::bind_front` through ADL. Qualify it as `amitgdev::bind_front`.

## Requirements

- C++23. The header uses explicit object parameters (deducing `this`) and `std::forward_like`.
- A toolchain that supports both: GCC 15+, Clang 18+, or a recent MSVC.
- The test program also uses `<source_location>`.
- No third-party dependencies.

## Tests

`src/main.cpp` is a standalone test program with no test framework. It exits with `0` only when every runtime check passes, and the type-level contract is verified with `static_assert`, so a regression in it fails the build. It can be registered directly with CTest.

It covers:

- `like_t` in isolation, for all four wrapper categories (`&`, `const&`, `&&`, `const&&`)
- the full matrix of wrapper categories against a callable with all four qualified call operators, checking `is_invocable`, `is_nothrow_invocable`, and which overload actually runs
- a deliberately mixed `noexcept` across those overloads, to prove propagation is per category
- fallback to a `const&` overload when only that overload exists
- non-viable categories (an `&`-only target is not invocable on `const` or rvalue wrappers) reported through `std::is_invocable` rather than a hard error
- argument order between bound and call-time arguments
- `noexcept` propagation from a function pointer and from a throwing functor
- a move-only bound argument, consumable only through an rvalue wrapper
- use in a constant expression

Build the project with the CMake preset for your platform (see `CMakePresets.json`) and run the resulting executable.

## Repository Layout

```
.
├── src/
│   ├── bind_front.hpp   # The utility (header-only)
│   └── main.cpp         # Standalone test program
├── CMakeLists.txt
└── CMakePresets.json
```

Build, formatting, and static-analysis infrastructure (CMake presets, CI, `.clang-format`, `.clang-tidy`) comes from the shared build template and is documented there.

---

**Last Updated:** 2026-10-02  
**Maintainer:** AmitGDev

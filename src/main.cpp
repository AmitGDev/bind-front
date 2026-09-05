// Standalone test driver for amitgdev::bind_front. No framework: the type-level
// contract (value category, noexcept, constraints) is verified with
// static_assert so a regression fails the build, and the few behaviors that
// need an actual call are checked at runtime.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <source_location>
#include <string>
#include <type_traits>
#include <utility>

#include "bind_front.hpp"

namespace {

enum class Tag : std::uint8_t {
  kLvalue,
  kConstLvalue,
  kRvalue,
  kConstRvalue,
};

// Intent: every qualifier has a distinct return tag (proves which overload was
// selected) and a deliberately mixed noexcept (proves noexcept is propagated
// per wrapper category rather than once for the whole wrapper).
struct Full {
  Tag operator()() & { return Tag::kLvalue; }

  Tag operator()() const& noexcept { return Tag::kConstLvalue; }

  Tag operator()() && noexcept { return Tag::kRvalue; }

  Tag operator()() const&& { return Tag::kConstRvalue; }
};

// Intent: only a const& overload exists, so rvalue wrappers must fall back to
// it, as they would for a plain call on the underlying object.
struct ConstRefOnly {
  Tag operator()() const& noexcept { return Tag::kConstLvalue; }
};

// Intent: only an lvalue overload exists, so every other wrapper category
// must be non-viable (constrained out) rather than a hard error.
struct LvalueOnly {
  void operator()() & {}
};

struct Add {
  constexpr int operator()(int lhs, int rhs) const noexcept {
    return lhs + rhs;
  }
};

struct MayThrow {
  int operator()(int value) const { return value; }
};

// Intent: consuming a unique_ptr by value is only possible when the wrapper
// hands the bound argument over as an rvalue.
struct Take {
  int operator()(std::unique_ptr<int> ptr) const { return *ptr; }
};

}  // namespace

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int g_failures = 0;

static int FreeAdd(int lhs, int rhs) noexcept {
  return lhs + rhs;
}

// Intent: report every failing check instead of aborting on the first one, so
// a single run shows the full extent of a regression.
static void
Check(bool condition, const char* what,
      std::source_location location = std::source_location::current()) {
  if (!condition) {
    ++g_failures;
    std::cerr << "FAIL line " << location.line() << ": " << what << '\n';
  }
}

namespace {

// like_t is the core of the design, so it is tested in isolation.
using amitgdev::internal::like_t;
static_assert(std::is_same_v<like_t<int&, Full>, Full&>);
static_assert(std::is_same_v<like_t<const int&, Full>, const Full&>);
static_assert(std::is_same_v<like_t<int, Full>, Full&&>);
static_assert(std::is_same_v<like_t<const int, Full>, const Full&&>);

// Matrix: 4 wrapper categories x qualifier-specific overloads and noexcept.
using FullWrapper = decltype(amitgdev::bind_front(Full{}));

static_assert(std::is_invocable_v<FullWrapper&>);
static_assert(std::is_invocable_v<const FullWrapper&>);
static_assert(std::is_invocable_v<FullWrapper>);
static_assert(std::is_invocable_v<const FullWrapper>);

static_assert(!std::is_nothrow_invocable_v<FullWrapper&>);
static_assert(std::is_nothrow_invocable_v<const FullWrapper&>);
static_assert(std::is_nothrow_invocable_v<FullWrapper>);
static_assert(!std::is_nothrow_invocable_v<const FullWrapper>);

// Fallback: rvalue wrappers bind to the const& overload.
using ConstRefWrapper = decltype(amitgdev::bind_front(ConstRefOnly{}));
static_assert(std::is_nothrow_invocable_v<ConstRefWrapper&>);
static_assert(std::is_nothrow_invocable_v<const ConstRefWrapper&>);
static_assert(std::is_nothrow_invocable_v<ConstRefWrapper>);
static_assert(std::is_nothrow_invocable_v<const ConstRefWrapper>);

// Non-viable: same wrapper object is callable as lvalue only.
using LvalueWrapper = decltype(amitgdev::bind_front(LvalueOnly{}));
static_assert(std::is_invocable_v<LvalueWrapper&>);
static_assert(!std::is_invocable_v<const LvalueWrapper&>);
static_assert(!std::is_invocable_v<LvalueWrapper>);
static_assert(!std::is_invocable_v<const LvalueWrapper>);

// Bound arguments plus call arguments, and noexcept from the callee.
using AddWrapper = decltype(amitgdev::bind_front(Add{}, 1));
static_assert(std::is_nothrow_invocable_v<AddWrapper&, int>);
static_assert(!std::is_invocable_v<AddWrapper&, std::string>);
static_assert(!std::is_invocable_v<AddWrapper&>);

using ThrowWrapper = decltype(amitgdev::bind_front(MayThrow{}));
static_assert(std::is_invocable_v<ThrowWrapper&, int>);
static_assert(!std::is_nothrow_invocable_v<ThrowWrapper&, int>);

// noexcept must survive a function pointer as the bound callable.
using FnPtrWrapper = decltype(amitgdev::bind_front(&FreeAdd, 1));
static_assert(std::is_nothrow_invocable_v<FnPtrWrapper&, int>);

// Move-only bound state: only an rvalue call can transfer ownership.
using MoveOnlyWrapper =
    decltype(amitgdev::bind_front(Take{}, std::make_unique<int>(0)));
static_assert(!std::is_invocable_v<MoveOnlyWrapper&>);
static_assert(!std::is_invocable_v<const MoveOnlyWrapper&>);
static_assert(std::is_invocable_v<MoveOnlyWrapper>);

// Usable in constant expressions.
constexpr auto kBoundAdd = amitgdev::bind_front(Add{}, 40);
static_assert(kBoundAdd(2) == 42);

}  // namespace

// Intent: iostream and allocation can throw; main must not let that escape, so
// the test body lives in its own function and main only translates failure.
static int RunTests() {
  // Runtime: the selected overload must match the wrapper's value category.
  auto full = amitgdev::bind_front(Full{});
  Check(full() == Tag::kLvalue, "lvalue wrapper -> & overload");
  Check(std::as_const(full)() == Tag::kConstLvalue,
        "const lvalue wrapper -> const& overload");
  Check(std::move(full)() == Tag::kRvalue, "rvalue wrapper -> && overload");
  Check(std::move(std::as_const(full))() == Tag::kConstRvalue,
        "const rvalue wrapper -> const&& overload");

  // Runtime: rvalue wrapper falls back to the only available const& overload.
  auto const_ref_only = amitgdev::bind_front(ConstRefOnly{});
  Check(std::move(const_ref_only)() == Tag::kConstLvalue,
        "rvalue wrapper falls back to const& overload");

  // Runtime: bound and call arguments are combined in order.
  auto add = amitgdev::bind_front(Add{}, 40);
  Check(add(2) == 42, "bound argument precedes call argument");

  auto fn_ptr = amitgdev::bind_front(&FreeAdd, 40);
  Check(fn_ptr(2) == 42, "function pointer bound callable");

  // Runtime: move-only bound state is handed over on an rvalue call.
  auto take = amitgdev::bind_front(Take{}, std::make_unique<int>(7));
  Check(std::move(take)() == 7, "move-only bound argument is forwarded");

  if (g_failures == 0) {
    std::cout << "All checks passed\n";
    return EXIT_SUCCESS;
  }
  std::cerr << g_failures << " check(s) failed\n";
  return EXIT_FAILURE;
}

int main() {
  try {
    return RunTests();
  } catch (...) {
    return EXIT_FAILURE;
  }
}
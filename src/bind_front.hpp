#ifndef AMITGDEV_BIND_FRONT_HPP_
#define AMITGDEV_BIND_FRONT_HPP_

/*
    bind_front.hpp
    Copyright (c) 2026, Amit Gefen

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace amitgdev {
namespace internal {

// Gives the type a bound member has when accessed through a wrapper of type
// Self, so the constraint and noexcept checks see exactly the cv/ref
// qualification that the real invocation will use.
template <typename Self, typename T>
using like_t = decltype(std::forward_like<Self>(std::declval<T&>()));

}  // namespace internal

// Result of bind_front. Behaves like the unspecified type returned by
// std::bind_front, but additionally propagates the noexcept status of the
// underlying invocation and constrains invalid calls out of the overload set,
// so std::is_invocable and std::is_nothrow_invocable are accurate independently
// of the standard library implementation in use.
//
// The call operator forwards the bound state with the value category and
// constness of the wrapper itself, so move-only bound arguments, const-only
// and rvalue-only call operators all behave as callers expect.
template <typename F, typename... Bound>
class BindFrontResult {
 public:
  template <typename Func, typename... BoundArgs>
  constexpr explicit BindFrontResult(std::in_place_t /*tag*/, Func&& func,
                                     BoundArgs&&... bound)
      : state_(std::forward<Func>(func), std::forward<BoundArgs>(bound)...) {}

  // NOLINTBEGIN(cppcoreguidelines-missing-std-forward)
  // Intent: args is forwarded in the body; the check does not see through the
  // explicit-object parameter list.
  template <typename Self, typename... Args>
    requires std::is_invocable_v<internal::like_t<Self, F>,
                                 internal::like_t<Self, Bound>..., Args...>
  constexpr decltype(auto)
  operator()(this Self&& self, Args&&... args) noexcept(
      std::is_nothrow_invocable_v<internal::like_t<Self, F>,
                                  internal::like_t<Self, Bound>..., Args...>) {
    // Intent: the tuple is forwarded with the wrapper's own cv/ref so each
    // element reaches std::invoke with the category the caller expects.
    return std::apply(
        [&](auto&&... parts) -> decltype(auto) {
          return std::invoke(std::forward<decltype(parts)>(parts)...,
                             std::forward<Args>(args)...);
        },
        std::forward_like<Self>(self.state_));
  }

  // NOLINTEND(cppcoreguidelines-missing-std-forward)

 private:
  // Intent: delegate construction and copy/move special members to
  // std::tuple instead of re-implementing them.
  std::tuple<F, Bound...> state_;
};

// Intent: keep the std spelling so call sites can switch between this and
// std::bind_front. Callers should qualify the call (amitgdev::bind_front) to
// avoid ambiguity with std::bind_front found through ADL.
// NOLINTBEGIN(readability-identifier-naming)
template <typename F, typename... Bound>
[[nodiscard]] constexpr auto bind_front(F&& func, Bound&&... bound) {
  // NOLINTEND(readability-identifier-naming)
  // Intent: enforce the construction and move-constructibility requirements
  // of the stored callable and bound arguments at the call site, rather than
  // deep inside tuple construction.
  static_assert(std::is_constructible_v<std::decay_t<F>, F> &&
                std::is_move_constructible_v<std::decay_t<F>>);
  static_assert((std::is_constructible_v<std::decay_t<Bound>, Bound> && ...) &&
                (std::is_move_constructible_v<std::decay_t<Bound>> && ...));

  return BindFrontResult<std::decay_t<F>, std::decay_t<Bound>...>(
      std::in_place, std::forward<F>(func), std::forward<Bound>(bound)...);
}

}  // namespace amitgdev

#endif  // AMITGDEV_BIND_FRONT_HPP_
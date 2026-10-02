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
#include <type_traits>
#include <utility>

namespace amitgdev {
namespace internal {

// Gives the type a captured member has when accessed through a closure of
// type Self, so noexcept and constraint checks see exactly the cv/ref
// qualification that the real invocation will use.
template <typename Self, typename T>
using like_t = decltype(std::forward_like<Self>(std::declval<T&>()));

}  // namespace internal

// Binds leading arguments to a callable, like std::bind_front, but propagates
// the noexcept status of the resulting invocation and constrains invalid calls
// out of the overload set. This makes std::is_invocable and
// std::is_nothrow_invocable report the correct result independently of the
// standard library implementation in use.
//
// The wrapper forwards the bound state with the value category and constness
// of the wrapper itself, so move-only bound arguments, const-only and
// rvalue-only call operators all behave as callers expect.
//
// Callers should qualify the call (amitgdev::bind_front) to avoid ambiguity
// with std::bind_front found through ADL.
// NOLINTBEGIN(readability-identifier-naming)
template <typename F, typename... Bound>
[[nodiscard]] constexpr auto bind_front(F&& func, Bound&&... bound) {
  // NOLINTEND(readability-identifier-naming)
  // NOLINTBEGIN(cppcoreguidelines-missing-std-forward)
  // Intent: args is forwarded in the body; the check does not see through the
  // explicit-object lambda parameter list.
  return
      [func = std::forward<F>(func),
       ... bound = std::forward<Bound>(bound)]<typename Self, typename... Args>(
          this Self&&,
          Args&&... args) noexcept(std::
                                       is_nothrow_invocable_v<
                                           internal::like_t<Self,
                                                            std::decay_t<F>>,
                                           internal::like_t<
                                               Self, std::decay_t<Bound>>...,
                                           Args...>) -> decltype(auto)
        requires std::is_invocable_v<
            internal::like_t<Self, std::decay_t<F>>,
            internal::like_t<Self, std::decay_t<Bound>>..., Args...>
  {
    return std::invoke(std::forward_like<Self>(func),
                       std::forward_like<Self>(bound)...,
                       std::forward<Args>(args)...);
  };
  // NOLINTEND(cppcoreguidelines-missing-std-forward)
}

}  // namespace amitgdev

#endif  // AMITGDEV_BIND_FRONT_HPP_
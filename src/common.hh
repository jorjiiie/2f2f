#pragma once

#include <atomic>
#include <type_traits>

namespace tftf {
template <class T> class atomic : public std::atomic<T> {
public:
  atomic() = default;
  constexpr atomic(T desired) : std::atomic<T>(desired) {}
  constexpr atomic(const atomic<T> &other)
      : atomic(other.load(std::memory_order_acquire)) {}
  atomic &operator=(const atomic<T> &other) {
    this->store(other.load(std::memory_order_relaxed),
                std::memory_order_release);
    return *this;
  }
};

template <class F> struct ScopeExit {
  template <class F_>
    requires std::is_convertible_v<F_, F>
  ScopeExit(F_ &&f) : m_f(std::forward<F_>(f)) {}

  ~ScopeExit() noexcept {
    if (armed) {
      m_f();
    }
  }
  void disarm() { armed = false; }
  F m_f;
  bool armed{true};
};

template <class F> auto on_scope_exit(F &&f) -> ScopeExit<F> {
  return ScopeExit<F>(std::forward<F>(f));
}
} // namespace tftf

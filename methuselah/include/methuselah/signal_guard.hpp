#pragma once

namespace methuselah {

class SignalGuard {
public:
  SignalGuard();
  ~SignalGuard();

  SignalGuard(const SignalGuard &) = delete;
  SignalGuard &operator=(const SignalGuard &) = delete;
  SignalGuard(SignalGuard &&) = delete;
  SignalGuard &operator=(SignalGuard &&) = delete;

  [[nodiscard]] static bool received() noexcept;
  [[nodiscard]] static int signum() noexcept;
};

} // namespace methuselah

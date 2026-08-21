#pragma once

#include <memory>
#include <optional>
#include <string>

namespace methuselah {

struct PromptOptions {
  std::string title;
  std::string message;
  int timeout_seconds = 0;
};

class UiSession {
public:
  UiSession();
  ~UiSession();

  UiSession(const UiSession &) = delete;
  UiSession &operator=(const UiSession &) = delete;
  UiSession(UiSession &&) noexcept;
  UiSession &operator=(UiSession &&) noexcept;

  static void silence_trace_logs();
  static void install_log_callback();

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] std::optional<std::string>
  prompt_password(const PromptOptions &options);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace methuselah

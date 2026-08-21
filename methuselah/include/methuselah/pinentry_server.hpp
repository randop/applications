#pragma once

#include <memory>
#include <string>

namespace methuselah {

class PinentryServer {
public:
  PinentryServer(std::string title, std::string dialog_message);
  ~PinentryServer();

  PinentryServer(const PinentryServer &) = delete;
  PinentryServer &operator=(const PinentryServer &) = delete;
  PinentryServer(PinentryServer &&) = delete;
  PinentryServer &operator=(PinentryServer &&) = delete;

  [[nodiscard]] int run();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace methuselah

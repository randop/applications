#pragma once

namespace methuselah {

class Application {
public:
  Application() = default;

  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;
  Application(Application &&) = delete;
  Application &operator=(Application &&) = delete;

  [[nodiscard]] int run(int argc, char **argv);
  void version();
};

} // namespace methuselah

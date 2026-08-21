#include "methuselah/logging.hpp"

#include "config.h"

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

namespace methuselah {

Logger::Logger() {
  auto console = spdlog::stderr_logger_mt("console");
  spdlog::set_default_logger(console);
  spdlog::set_level(SPDLOG_DEFAULT_LEVEL);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
}

Logger::~Logger() { spdlog::shutdown(); }

} // namespace methuselah

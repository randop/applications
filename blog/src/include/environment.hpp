#pragma once

#ifndef BLOG_ENVIRONMENT_HPP
#define BLOG_ENVIRONMENT_HPP

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <sys/sysinfo.h>
#include <sys/utsname.h>

#include <boost/optional.hpp>
#include <spdlog/spdlog.h>

class Environment {
public:
  // Retrieves the value of the specified environment variable.
  // Returns boost::optional containing the value if found, or boost::none if
  // not set.
  static boost::optional<std::string> getVariable(const std::string &name);

  static void logOSinfo();

private:
  static std::string formatBytes(uint64_t bytes);
};

boost::optional<std::string> Environment::getVariable(const std::string &name) {
  if (const char *value = std::getenv(name.c_str())) {
    return std::string(value);
  }
  return boost::none;
}

std::string Environment::formatBytes(uint64_t bytes) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit_index = 0;
  double size = static_cast<double>(bytes);

  while (size >= 1024 && unit_index < 4) {
    size /= 1024;
    unit_index++;
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
  return oss.str();
}

void Environment::logOSinfo() {
  struct utsname info;
  if (uname(&info) == -1) {
    spdlog::error("OS info error: ", std::strerror(errno));
  } else {
    spdlog::info("OS info: {} {} {} {} {}", info.sysname, info.nodename,
                 info.release, info.version, info.machine);
  }

  // Get memory info
  struct sysinfo mem_info;
  if (sysinfo(&mem_info) == -1) {
    spdlog::error("Error retrieving memory info: {}", std::strerror(errno));
  } else {
    // Print total memory
    uint64_t total_memory = mem_info.totalram * mem_info.mem_unit;
    spdlog::info("RAM: {}", formatBytes(total_memory));
  }
}

#endif // BLOG_ENVIRONMENT_HPP

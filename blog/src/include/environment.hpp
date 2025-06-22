#pragma once

#ifndef BLOG_ENVIRONMENT_HPP
#define BLOG_ENVIRONMENT_HPP

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/utsname.h>

#include <spdlog/spdlog.h>
#include <boost/optional.hpp>

class Environment {
public:
  // Retrieves the value of the specified environment variable.
  // Returns boost::optional containing the value if found, or boost::none if
  // not set.
  static boost::optional<std::string> getVariable(const std::string &name);

  static void logOSinfo();

private:
  // No private members needed for this implementation.
};

boost::optional<std::string> Environment::getVariable(const std::string &name) {
  if (const char *value = std::getenv(name.c_str())) {
    return std::string(value);
  }
  return boost::none;
}

void Environment::logOSinfo() {
  struct utsname info;
  if (uname(&info) == -1) {
    spdlog::error("OS info error: ", errno);
  } else {
    spdlog::info("OS info: {} {} {} {} {}", info.sysname, info.nodename, info.release, info.version, info.machine);
  }
}

#endif // BLOG_ENVIRONMENT_HPP

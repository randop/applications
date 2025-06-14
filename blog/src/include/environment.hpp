#pragma once

#ifndef BLOG_ENVIRONMENT_HPP
#define BLOG_ENVIRONMENT_HPP

#include <boost/optional.hpp>
#include <cstdlib>
#include <string>

class Environment {
public:
  // Retrieves the value of the specified environment variable.
  // Returns boost::optional containing the value if found, or boost::none if
  // not set.
  static boost::optional<std::string> getVariable(const std::string &name);

private:
  // No private members needed for this implementation.
};

boost::optional<std::string> Environment::getVariable(const std::string &name) {
  if (const char *value = std::getenv(name.c_str())) {
    return std::string(value);
  }
  return boost::none;
}

#endif // BLOG_ENVIRONMENT_HPP

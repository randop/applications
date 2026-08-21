#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace methuselah {

class GitPredicate {
public:
  [[nodiscard]] static GitPredicate parse(const std::string &input);
  [[nodiscard]] static GitPredicate read_from_stdin();

  void write_credentials(std::string_view password) const;

  [[nodiscard]] bool has_capability(const std::string &cap) const;
  [[nodiscard]] bool has_wwwauth(const std::string &auth) const;

  [[nodiscard]] const std::string &protocol() const noexcept {
    return protocol_;
  }
  [[nodiscard]] const std::string &host() const noexcept { return host_; }
  [[nodiscard]] const std::string &username() const noexcept {
    return username_;
  }
  [[nodiscard]] const std::string &password() const noexcept {
    return password_;
  }

private:
  std::vector<std::string> capabilities_;
  std::vector<std::string> wwwauth_;
  std::string protocol_;
  std::string host_;
  std::string username_;
  std::string password_;
};

} // namespace methuselah

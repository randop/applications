#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace methuselah {

class Config {
public:
  [[nodiscard]] static Config load(const std::filesystem::path &path);

  [[nodiscard]] std::optional<std::string>
  gpg_file_for(std::string_view section) const;

private:
  std::vector<std::pair<std::string, std::string>> entries_;
};

} // namespace methuselah

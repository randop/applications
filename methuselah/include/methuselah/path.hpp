#pragma once

#include <filesystem>
#include <string>

namespace methuselah {

[[nodiscard]] std::filesystem::path home_path();
[[nodiscard]] std::filesystem::path resolve_path(std::string path);

} // namespace methuselah

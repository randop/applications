#include "methuselah/path.hpp"

#include <cstddef>
#include <pwd.h>
#include <stdexcept>
#include <unistd.h>
#include <utility>

namespace methuselah {
namespace fs = std::filesystem;

fs::path home_path() {
  if (struct passwd *pw = getpwuid(getuid()); pw && pw->pw_dir) {
    return fs::path(pw->pw_dir);
  }
  throw std::runtime_error("Failed to get home directory");
}

fs::path resolve_path(std::string path) {
  const fs::path home = home_path();

  if (path.starts_with("~/")) {
    path.replace(0, 1, home.string());
  } else if (path == "~") {
    return home;
  }

  std::size_t pos = 0;
  while ((pos = path.find("$HOME", pos)) != std::string::npos) {
    path.replace(pos, 5, home.string());
    pos += home.string().length();
  }

  pos = 0;
  while ((pos = path.find("${HOME}", pos)) != std::string::npos) {
    path.replace(pos, 7, home.string());
    pos += home.string().length();
  }

  fs::path p(std::move(path));

  if (p.is_relative()) {
    p = fs::absolute(p);
  }

  return fs::weakly_canonical(p);
}

} // namespace methuselah

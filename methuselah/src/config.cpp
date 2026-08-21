#include "methuselah/config.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace methuselah {

Config Config::load(const std::filesystem::path &path) {
  boost::property_tree::ptree pt;
  boost::property_tree::ini_parser::read_ini(path.string(), pt);

  Config config;
  for (const auto &[host, subtree] : pt) {
    config.entries_.emplace_back(host, subtree.get<std::string>("gpgfile", ""));
  }
  return config;
}

std::optional<std::string>
Config::gpg_file_for(std::string_view section) const {
  for (const auto &[name, gpgfile] : entries_) {
    if (boost::algorithm::iequals(name, section)) {
      if (gpgfile.empty()) {
        return std::nullopt;
      }
      return gpgfile;
    }
  }
  return std::nullopt;
}

} // namespace methuselah

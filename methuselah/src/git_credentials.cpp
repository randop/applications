#include "methuselah/git_credentials.hpp"

#include <algorithm>
#include <boost/property_tree/ptree.hpp>
#include <cstddef>
#include <iostream>
#include <sstream>

namespace methuselah {

GitPredicate GitPredicate::parse(const std::string &input) {
  GitPredicate predicate;

  namespace pt = boost::property_tree;
  pt::ptree tree;

  std::istringstream iss(input);
  std::string line;

  while (std::getline(iss, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const std::size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      continue;
    }

    std::string key = line.substr(0, eq_pos);
    std::string value = line.substr(eq_pos + 1);

    if (key.size() >= 2 && key.substr(key.size() - 2) == "[]") {
      key = key.substr(0, key.size() - 2);
      pt::ptree child;
      child.put("", value);
      tree.add_child(key, child);
    } else {
      tree.put(key, value);
    }
  }

  if (tree.count("capability") > 0) {
    for (const auto &item : tree.get_child("capability")) {
      predicate.capabilities_.push_back(item.second.data());
    }
  }

  if (tree.count("wwwauth") > 0) {
    for (const auto &item : tree.get_child("wwwauth")) {
      predicate.wwwauth_.push_back(item.second.data());
    }
  }

  predicate.protocol_ = tree.get("protocol", "");
  predicate.host_ = tree.get("host", "");
  predicate.username_ = tree.get("username", "");
  predicate.password_ = tree.get("password", "");

  return predicate;
}

GitPredicate GitPredicate::read_from_stdin() {
  std::string line;
  std::string inputs;

  while (std::getline(std::cin, line)) {
    if (line.empty()) {
      break;
    }
    if (!inputs.empty()) {
      inputs.append("\n");
    }
    inputs.append(line);
  }

  return parse(inputs);
}

void GitPredicate::write_credentials(std::string_view password) const {
  std::cout << "protocol=" << protocol_ << "\nhost=" << host_
            << "\nusername=" << username_ << "\npassword=" << password
            << std::endl;
}

bool GitPredicate::has_capability(const std::string &cap) const {
  return std::find(capabilities_.begin(), capabilities_.end(), cap) !=
         capabilities_.end();
}

bool GitPredicate::has_wwwauth(const std::string &auth) const {
  return std::find(wwwauth_.begin(), wwwauth_.end(), auth) != wwwauth_.end();
}

} // namespace methuselah

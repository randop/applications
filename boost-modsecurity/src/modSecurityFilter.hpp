#ifndef MODSECURITY_FILTER_HPP
#define MODSECURITY_FILTER_HPP

#include "common.hpp"
#include <memory>
#include <modsecurity/modsecurity.h>
#include <modsecurity/transaction.h>

namespace App {

class ModSecurityFilter {
public:
  ModSecurityFilter();
  ~ModSecurityFilter();

  bool ProcessRequest(StringView method, StringView uri, StringView body,
                      StringView client_ip);

private:
  std::unique_ptr<modsecurity::ModSecurity> modsec_;
  std::unique_ptr<modsecurity::RulesSet> rules_;
};

} // namespace App

#endif // MODSECURITY_FILTER_HPP
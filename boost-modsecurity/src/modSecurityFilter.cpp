#include "modSecurityFilter.hpp"
#include <modsecurity/modsecurity.h>
#include <modsecurity/rules_set.h>

namespace App {

ModSecurityFilter::ModSecurityFilter() {
  modsec_ = std::make_unique<modsecurity::ModSecurity>();
  rules_ = std::make_unique<modsecurity::RulesSet>();

  // Load OWASP CRS rules
  const char* rules_dir = "rules/owasp-crs/rules/";
  rules_->loadFromFile(rules_dir + std::string("REQUEST-901-INITIALIZATION.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-910-IP-REPUTATION.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-911-METHOD-ENFORCEMENT.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-912-DOS-PROTECTION.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-913-SCANNER-DETECTION.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-920-PROTOCOL-ENFORCEMENT.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-921-PROTOCOL-ATTACK.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-930-APPLICATION-ATTACK-LFI.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-931-APPLICATION-ATTACK-RFI.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-932-APPLICATION-ATTACK-RCE.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-933-APPLICATION-ATTACK-PHP.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-941-APPLICATION-ATTACK-XSS.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-942-APPLICATION-ATTACK-SQLI.conf"));
  rules_->loadFromFile(rules_dir + std::string("REQUEST-943-APPLICATION-ATTACK-SESSION-FIXATION.conf"));
  rules_->loadFromFile(rules_dir + std::string("RESPONSE-950-DATA-LEAKAGES.conf"));
  rules_->loadFromFile(rules_dir + std::string("RESPONSE-951-DATA-LEAKAGES-SQL.conf"));
  rules_->loadFromFile(rules_dir + std::string("RESPONSE-952-DATA-LEAKAGES-JAVA.conf"));
  rules_->loadFromFile(rules_dir + std::string("RESPONSE-953-DATA-LEAKAGES-PHP.conf"));
  rules_->loadFromFile(rules_dir + std::string("RESPONSE-954-DATA-LEAKAGES-IIS.conf"));

  modsec_->setServerLogCb([](void*, const void*) -> int { return 0; }, modsecurity::LogProperty::TextLogProperty);
}

ModSecurityFilter::~ModSecurityFilter() = default;

bool ModSecurityFilter::ProcessRequest(StringView method, StringView uri,
                                      StringView body, StringView client_ip) {
  auto transaction = std::make_unique<modsecurity::Transaction>(modsec_.get(), rules_.get(), nullptr);

  // Process connection
  transaction->processConnection(client_ip.data(), 0, "0.0.0.0", 0);

  // Process URI and method
  transaction->processURI(uri.data(), method.data(), "HTTP/1.1");

  // Process request body
  if (!body.empty()) {
    transaction->appendRequestBody(
        reinterpret_cast<const unsigned char*>(body.data()), body.size());
    transaction->processRequestBody();
  }

  // Check if intervention is needed
  modsecurity::ModSecurityIntervention intervention{};
  return transaction->intervention(&intervention) == 0;
}

}  // namespace App
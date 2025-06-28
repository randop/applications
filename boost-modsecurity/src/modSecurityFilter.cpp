#include "modSecurityFilter.hpp"
#include "common.hpp"
#include <modsecurity/modsecurity.h>
#include <modsecurity/rules_set.h>

namespace App {

ModSecurityFilter::ModSecurityFilter() {
  modsec_ = std::make_unique<modsecurity::ModSecurity>();
  rules_ = std::make_unique<modsecurity::RulesSet>();

  // Load OWASP CRS rules
  std::string rules_dir = "rules/owasp-crs/";
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-901-INITIALIZATION.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-910-IP-REPUTATION.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-911-METHOD-ENFORCEMENT.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-912-DOS-PROTECTION.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-913-SCANNER-DETECTION.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-920-PROTOCOL-ENFORCEMENT.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-921-PROTOCOL-ATTACK.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-930-APPLICATION-ATTACK-LFI.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-931-APPLICATION-ATTACK-RFI.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-932-APPLICATION-ATTACK-RCE.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-933-APPLICATION-ATTACK-PHP.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-941-APPLICATION-ATTACK-XSS.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-942-APPLICATION-ATTACK-SQLI.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "REQUEST-943-APPLICATION-ATTACK-SESSION-FIXATION.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "RESPONSE-950-DATA-LEAKAGES.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "RESPONSE-951-DATA-LEAKAGES-SQL.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "RESPONSE-952-DATA-LEAKAGES-JAVA.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "RESPONSE-953-DATA-LEAKAGES-PHP.conf").c_str());
  rules_->loadFromUri(std::string(rules_dir + "RESPONSE-954-DATA-LEAKAGES-IIS.conf").c_str());

  /** TODO: Fix logging
  modsec_->setServerLogCb([](void*, const void*) -> int { return 0; }, modsecurity::LogProperty::TextLogProperty);
   */
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
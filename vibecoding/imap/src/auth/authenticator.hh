#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace imap {

// Trivial in-process password map for the barebones server.
class authenticator {
public:
    authenticator();

    [[nodiscard]] bool check(std::string_view user, std::string_view password) const;

private:
    std::unordered_map<std::string, std::string> _users;
};

} // namespace imap

#include "auth/authenticator.hh"

namespace imap {

authenticator::authenticator() {
    _users.emplace("demo", "demo");
    _users.emplace("alice", "secret");
}

bool authenticator::check(std::string_view user, std::string_view password) const {
    auto it = _users.find(std::string(user));
    if (it == _users.end()) {
        return false;
    }
    return it->second == password;
}

} // namespace imap

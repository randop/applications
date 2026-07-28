#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace nftmgr {

// Thrown whenever an nft statement fails to parse or fails to commit.
// Carries the exact statement that was sent and, when nftables provided
// one, its own diagnostic text (a syntax error with a caret, "File exists"
// for a conflicting create, "No such file or directory" for a missing
// object, etc). Netlink-level failures sometimes provide no diagnostic
// text at all -- detail() will be empty in that case.
class NftError : public std::runtime_error {
public:
    NftError(std::string statement, std::string detail)
        : std::runtime_error(format(statement, detail)),
          statement_(std::move(statement)),
          detail_(std::move(detail)) {}

    [[nodiscard]] const std::string& statement() const noexcept { return statement_; }
    [[nodiscard]] const std::string& detail() const noexcept { return detail_; }

private:
    static std::string format(const std::string& statement, const std::string& detail) {
        std::string msg = "nftables command failed: ";
        msg += statement;
        if (!detail.empty()) {
            msg += "\n";
            msg += detail;
        }
        return msg;
    }

    std::string statement_;
    std::string detail_;
};

} // namespace nftmgr

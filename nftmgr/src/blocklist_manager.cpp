#include "nftmgr/blocklist_manager.hpp"
#include "nftmgr/nft_error.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>

#include <format>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace nftmgr {

namespace {

// RAII helper so an exception thrown mid-query can't leave the underlying
// NftContext stuck in JSON output mode for unrelated later calls.
class ScopedJsonOutput {
public:
    explicit ScopedJsonOutput(NftContext& ctx) : ctx_(ctx) { ctx_.set_json_output(true); }
    ~ScopedJsonOutput() { ctx_.set_json_output(false); }
    ScopedJsonOutput(const ScopedJsonOutput&) = delete;
    ScopedJsonOutput& operator=(const ScopedJsonOutput&) = delete;

private:
    NftContext& ctx_;
};

} // namespace

NormalizedAddress normalize_address(std::string_view address) {
    const std::string addr(address);
    unsigned char buf[sizeof(struct in6_addr)];

    if (inet_pton(AF_INET, addr.c_str(), buf) == 1) {
        char text[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, buf, text, sizeof(text));
        return {std::string(text), false};
    }
    if (inet_pton(AF_INET6, addr.c_str(), buf) == 1) {
        char text[INET6_ADDRSTRLEN] = {};
        inet_ntop(AF_INET6, buf, text, sizeof(text));
        return {std::string(text), true};
    }
    throw std::invalid_argument(std::format("'{}' is not a valid IPv4 or IPv6 address", address));
}

BlocklistManager::BlocklistManager(BlocklistConfig config) : config_(std::move(config)) {}

void BlocklistManager::ensure_infrastructure() {
    ctx_.run(std::format("add table {} {}", config_.family, config_.table));

    ctx_.run(std::format(
        "add chain {} {} {} {{ type filter hook {} priority {}; policy accept; }}",
        config_.family, config_.table, config_.chain, config_.chain, config_.priority));

    ctx_.run(std::format(
        "add set {} {} {} {{ type ipv4_addr; flags timeout; }}",
        config_.family, config_.table, config_.set4));

    ctx_.run(std::format(
        "add set {} {} {} {{ type ipv6_addr; flags timeout; }}",
        config_.family, config_.table, config_.set6));

    add_managed_rule_if_absent(config_.set4, "ip", "nftmgr-managed-v4");
    add_managed_rule_if_absent(config_.set6, "ip6", "nftmgr-managed-v6");
}

void BlocklistManager::add_managed_rule_if_absent(const std::string& set_name,
                                                    std::string_view proto_field,
                                                    std::string_view comment) {
    if (chain_has_rule_with_comment(comment)) {
        return; // `add rule` isn't idempotent -- guard manually via a tag comment
    }
    ctx_.run(std::format(
        R"(add rule {} {} {} {} saddr @{} counter drop comment "{}")",
        config_.family, config_.table, config_.chain, proto_field, set_name, comment));
}

bool BlocklistManager::chain_has_rule_with_comment(std::string_view comment) {
    std::string raw;
    {
        ScopedJsonOutput json_scope(ctx_);
        try {
            raw = ctx_.query(std::format("list chain {} {} {}", config_.family, config_.table, config_.chain));
        } catch (const NftError&) {
            return false; // chain doesn't exist yet
        }
    }

    auto doc = nlohmann::json::parse(raw, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) {
        return false;
    }
    for (const auto& item : doc.value("nftables", nlohmann::json::array())) {
        if (item.contains("rule") && item["rule"].value("comment", "") == comment) {
            return true;
        }
    }
    return false;
}

void BlocklistManager::block(std::string_view address, std::optional<std::chrono::seconds> ttl) {
    const auto normalized = normalize_address(address);
    const std::string& set_name = normalized.is_v6 ? config_.set6 : config_.set4;

    std::string element = normalized.text;
    if (ttl) {
        element += std::format(" timeout {}s", ttl->count());
    }

    ctx_.run(std::format(
        "add element {} {} {} {{ {} }}", config_.family, config_.table, set_name, element));
}

void BlocklistManager::unblock(std::string_view address) {
    const auto normalized = normalize_address(address);
    const std::string& set_name = normalized.is_v6 ? config_.set6 : config_.set4;

    try {
        ctx_.run(std::format(
            "delete element {} {} {} {{ {} }}",
            config_.family, config_.table, set_name, normalized.text));
    } catch (const NftError& e) {
        // Deleting an address that isn't currently in the set fails with
        // "No such file or directory". The caller wants "not blocked" as
        // the end state either way, so treat that specific failure as a
        // no-op success instead of propagating it.
        if (e.detail().find("No such file or directory") == std::string::npos) {
            throw;
        }
    }
}

bool BlocklistManager::is_blocked(std::string_view address) {
    const auto normalized = normalize_address(address);
    const std::string& set_name = normalized.is_v6 ? config_.set6 : config_.set4;

    for (const auto& entry : list_set(set_name)) {
        if (entry.address == normalized.text) {
            return true;
        }
    }
    return false;
}

std::vector<BlockedAddress> BlocklistManager::list_blocked() {
    auto result = list_set(config_.set4);
    auto v6 = list_set(config_.set6);
    result.insert(result.end(), std::make_move_iterator(v6.begin()), std::make_move_iterator(v6.end()));
    return result;
}

std::vector<BlockedAddress> BlocklistManager::list_set(const std::string& set_name) {
    std::vector<BlockedAddress> result;

    std::string raw;
    {
        ScopedJsonOutput json_scope(ctx_);
        try {
            raw = ctx_.query(std::format("list set {} {} {}", config_.family, config_.table, set_name));
        } catch (const NftError&) {
            return result; // set doesn't exist yet -- nothing blocked
        }
    }

    auto doc = nlohmann::json::parse(raw, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) {
        return result;
    }

    for (const auto& item : doc.value("nftables", nlohmann::json::array())) {
        if (!item.contains("set")) {
            continue;
        }
        // Elements without a timeout serialize as bare JSON strings;
        // elements with one (or a comment) serialize as
        // {"elem": {"val": ..., "timeout": N, "expires": M}}. "expires" is
        // the *remaining* time, which is what callers actually want.
        for (const auto& e : item["set"].value("elem", nlohmann::json::array())) {
            if (e.is_string()) {
                result.push_back({e.get<std::string>(), std::nullopt});
            } else if (e.is_object() && e.contains("elem")) {
                const auto& inner = e["elem"];
                std::string val = inner.value("val", "");
                if (inner.contains("expires")) {
                    result.push_back({std::move(val), std::chrono::seconds(inner["expires"].get<long long>())});
                } else {
                    result.push_back({std::move(val), std::nullopt});
                }
            }
        }
    }
    return result;
}

void BlocklistManager::flush() {
    ctx_.run(std::format("flush set {} {} {}", config_.family, config_.table, config_.set4));
    ctx_.run(std::format("flush set {} {} {}", config_.family, config_.table, config_.set6));
}

} // namespace nftmgr

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nftmgr/nft_context.hpp"

namespace nftmgr {

struct BlockedAddress {
    std::string address;
    // nullopt means the entry is permanent (no timeout was set when
    // blocked); otherwise this is the time remaining until nftables
    // auto-expires it.
    std::optional<std::chrono::seconds> expires_in;
};

struct BlocklistConfig {
    std::string family = "inet";
    std::string table  = "nftmgr";
    // Must be a valid hook name (input/output/forward/prerouting/
    // postrouting) -- this tool always creates and hooks its own base
    // chain rather than attaching to an existing one, so infrastructure
    // setup stays fully self-contained and idempotent.
    std::string chain = "input";
    std::string set4  = "blocklist4";
    std::string set6  = "blocklist6";
    int priority      = 0;
};

// fail2ban-style address blocking backed by nftables sets.
//
// Rather than inserting/removing one rule per banned address, this keeps a
// single drop rule per address family that matches against a set, so
// blocking/unblocking is an O(1) set-element operation and nftables can
// auto-expire entries via per-element timeouts. This is the approach
// nftables' own documentation recommends over one-rule-per-address once
// you're past a handful of entries.
//
// Not copyable/movable (owns an NftContext, which owns a mutex).
class BlocklistManager {
public:
    explicit BlocklistManager(BlocklistConfig config = {});

    // Creates the table, chain (hooked at the configured priority), both
    // address-family sets, and their drop rules if they don't already
    // exist. Safe to call on every process startup: table/chain/set
    // creation is idempotent in nftables, and rule creation is guarded
    // manually here (rule creation itself is NOT idempotent -- calling
    // `add rule` twice creates two identical rules).
    void ensure_infrastructure();

    // Blocks an IPv4 or IPv6 address (auto-detected). `ttl` of nullopt
    // blocks permanently, until explicitly unblocked or flush()ed.
    // Throws std::invalid_argument if `address` isn't a syntactically
    // valid address -- addresses are interpolated into nft statements, so
    // this validation also serves as the injection guard.
    void block(std::string_view address, std::optional<std::chrono::seconds> ttl = std::nullopt);

    // Lifts a ban. Not an error if the address wasn't blocked.
    void unblock(std::string_view address);

    [[nodiscard]] bool is_blocked(std::string_view address);

    [[nodiscard]] std::vector<BlockedAddress> list_blocked();

    // Removes every blocked address without touching table/chain/rules.
    void flush();

private:
    [[nodiscard]] std::vector<BlockedAddress> list_set(const std::string& set_name);
    [[nodiscard]] bool chain_has_rule_with_comment(std::string_view comment);
    void add_managed_rule_if_absent(const std::string& set_name, std::string_view proto_field,
                                     std::string_view comment);

    BlocklistConfig config_;
    NftContext ctx_;
};

// Validates and canonicalizes an address (e.g. expands/collapses IPv6
// forms consistently) via inet_pton/inet_ntop. Throws std::invalid_argument
// if it isn't a syntactically valid IPv4 or IPv6 address.
struct NormalizedAddress {
    std::string text;
    bool is_v6;
};
[[nodiscard]] NormalizedAddress normalize_address(std::string_view address);

} // namespace nftmgr

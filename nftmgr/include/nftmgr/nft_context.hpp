#pragma once

#include <mutex>
#include <string>
#include <string_view>

struct nft_ctx; // forward decl of libnftables' opaque context type

namespace nftmgr {

// Thread-safe RAII wrapper around libnftables' nft_ctx.
//
// libnftables is not safe to call concurrently from multiple threads on the
// same nft_ctx, so every operation here is serialized behind an internal
// mutex. Multiple statements passed to run()/query() in a single call are
// committed as one atomic netlink transaction -- exactly like running
// `nft -f somefile.nft` where the file has several lines in it. That's the
// same mechanism BlocklistManager uses to make ensure_infrastructure()
// safe: either all of table/chain/set/rule land, or none do.
class NftContext {
public:
    NftContext();
    ~NftContext();

    NftContext(const NftContext&) = delete;
    NftContext& operator=(const NftContext&) = delete;
    NftContext(NftContext&&) = delete;
    NftContext& operator=(NftContext&&) = delete;

    // Runs one or more nft statements. Throws NftError on failure.
    void run(std::string_view script);

    // Like run(), but returns nftables' textual/JSON output -- use for
    // `list ...` style queries. Throws NftError on failure.
    std::string query(std::string_view script);

    // Toggles JSON-formatted output for subsequent query() calls.
    void set_json_output(bool enabled);

    // When enabled, statements are parsed and validated but never
    // committed to the kernel (useful for a --dry-run CLI flag).
    void set_dry_run(bool enabled);

private:
    std::string run_locked(std::string_view script, bool want_output);

    nft_ctx* ctx_;
    std::mutex mutex_;
};

} // namespace nftmgr

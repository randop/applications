#include "nftmgr/nft_context.hpp"
#include "nftmgr/nft_error.hpp"

extern "C" {
#include <nftables/libnftables.h>
}

#include <string>

namespace nftmgr {

NftContext::NftContext() : ctx_(nft_ctx_new(NFT_CTX_DEFAULT)) {
    if (!ctx_) {
        throw NftError("nft_ctx_new", "failed to allocate nft_ctx (out of memory?)");
    }
}

NftContext::~NftContext() {
    if (ctx_) {
        nft_ctx_free(ctx_);
    }
}

void NftContext::set_json_output(bool enabled) {
    std::lock_guard lock(mutex_);
    nft_ctx_output_set_flags(ctx_, enabled ? static_cast<unsigned int>(NFT_CTX_OUTPUT_JSON) : 0u);
}

void NftContext::set_dry_run(bool enabled) {
    std::lock_guard lock(mutex_);
    nft_ctx_set_dry_run(ctx_, enabled);
}

std::string NftContext::run_locked(std::string_view script, bool want_output) {
    // libnftables gotcha (verified against libnftables 1.0.9): output/error
    // buffering must be (re-)armed with nft_ctx_buffer_{output,error}()
    // before *every* call, and the pointer from nft_ctx_get_*_buffer() must
    // be read immediately afterwards -- do NOT call
    // nft_ctx_unbuffer_{output,error}() first. Unbuffering tears down the
    // underlying memstream and the buffer reads back empty even though the
    // command produced output or a diagnostic. Re-arming before the next
    // call correctly resets the buffer rather than appending to stale data.
    nft_ctx_buffer_output(ctx_);
    nft_ctx_buffer_error(ctx_);

    // nft_run_cmd_from_buffer() takes a plain (non-const-propagated) char*
    // parameter; keep our own owned copy alive for the duration of the call.
    const std::string owned_script(script);
    const int rc = nft_run_cmd_from_buffer(ctx_, owned_script.c_str());

    const char* out = nft_ctx_get_output_buffer(ctx_);
    const char* err = nft_ctx_get_error_buffer(ctx_);

    if (rc != 0) {
        // Note: not every failure comes with diagnostic text. Parser
        // errors (bad syntax) always do; some netlink-level failures
        // (e.g. referencing a table that doesn't exist) currently don't,
        // in which case detail() is simply empty.
        throw NftError(owned_script, err ? err : "");
    }

    if (want_output && out) {
        return std::string(out);
    }
    return {};
}

void NftContext::run(std::string_view script) {
    std::lock_guard lock(mutex_);
    run_locked(script, /*want_output=*/false);
}

std::string NftContext::query(std::string_view script) {
    std::lock_guard lock(mutex_);
    return run_locked(script, /*want_output=*/true);
}

} // namespace nftmgr

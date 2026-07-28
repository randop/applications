#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "nftmgr/blocklist_manager.hpp"
#include "nftmgr/nft_error.hpp"

namespace {

void print_usage(const char* prog) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s init                   create table/chain/sets/rules\n"
        "  %s block <addr> [ttl]     ban an address; ttl like 30, 10m, 2h, 1d (default: permanent)\n"
        "  %s unblock <addr>         lift a ban (no-op if not currently blocked)\n"
        "  %s status <addr>          check whether an address is banned\n"
        "  %s list                   list all banned addresses\n"
        "  %s flush                  lift every ban\n"
        "\n"
        "Requires CAP_NET_ADMIN (run as root, or grant the capability to the binary).\n",
        prog, prog, prog, prog, prog, prog);
}

std::optional<std::chrono::seconds> parse_ttl(std::string_view s) {
    if (s.empty()) {
        return std::nullopt;
    }
    long multiplier = 1;
    std::string_view digits = s;
    switch (s.back()) {
        case 's': multiplier = 1;     digits.remove_suffix(1); break;
        case 'm': multiplier = 60;    digits.remove_suffix(1); break;
        case 'h': multiplier = 3600;  digits.remove_suffix(1); break;
        case 'd': multiplier = 86400; digits.remove_suffix(1); break;
        default: break; // bare number == seconds
    }
    const std::string digits_owned(digits);
    char* end = nullptr;
    const long value = std::strtol(digits_owned.c_str(), &end, 10);
    if (value <= 0 || end == digits_owned.c_str() || *end != '\0') {
        throw std::invalid_argument(std::format("invalid ttl '{}'", s));
    }
    return std::chrono::seconds(value * multiplier);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string_view cmd = argv[1];

    try {
        nftmgr::BlocklistManager mgr;

        if (cmd == "init") {
            mgr.ensure_infrastructure();
            std::puts("nftmgr: infrastructure ready");

        } else if (cmd == "block") {
            if (argc < 3) { print_usage(argv[0]); return 1; }
            mgr.ensure_infrastructure();
            const auto ttl = argc >= 4 ? parse_ttl(argv[3]) : std::nullopt;
            mgr.block(argv[2], ttl);
            if (ttl) {
                std::printf("nftmgr: blocked %s for %llds\n", argv[2],
                            static_cast<long long>(ttl->count()));
            } else {
                std::printf("nftmgr: blocked %s (permanent)\n", argv[2]);
            }

        } else if (cmd == "unblock") {
            if (argc < 3) { print_usage(argv[0]); return 1; }
            mgr.unblock(argv[2]);
            std::printf("nftmgr: unblocked %s\n", argv[2]);

        } else if (cmd == "status") {
            if (argc < 3) { print_usage(argv[0]); return 1; }
            const bool blocked = mgr.is_blocked(argv[2]);
            std::printf("%s: %s\n", argv[2], blocked ? "BLOCKED" : "not blocked");
            return blocked ? 0 : 1;

        } else if (cmd == "list") {
            for (const auto& entry : mgr.list_blocked()) {
                if (entry.expires_in) {
                    std::printf("%-40s expires in %llds\n", entry.address.c_str(),
                                static_cast<long long>(entry.expires_in->count()));
                } else {
                    std::printf("%-40s permanent\n", entry.address.c_str());
                }
            }

        } else if (cmd == "flush") {
            mgr.flush();
            std::puts("nftmgr: all bans lifted");

        } else {
            print_usage(argv[0]);
            return 1;
        }

    } catch (const nftmgr::NftError& e) {
        std::fprintf(stderr, "nftmgr: %s\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nftmgr: %s\n", e.what());
        return 1;
    }

    return 0;
}

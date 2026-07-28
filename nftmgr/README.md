# nftmgr

A small C++23 library + CLI for maintaining an nftables-backed IP blocklist
(fail2ban-style: ban/unban/list/expire addresses), built directly on
`libnftables` — no shelling out to `nft`, no parsing CLI text output.

Built and smoke-tested against `libnftables 1.0.9` on Linux 6.x with a live
netlink/nf_tables backend (not just compiled — actually exercised: init,
block, unblock, TTL expiry fields, double-init idempotency, and bad-address
rejection all run against the real kernel during development).

## Design

- **One set per address family, not one rule per address.** `blocklist4`
  and `blocklist6` are nftables sets; a single `drop` rule per family
  matches `ip saddr @blocklist4` / `ip6 saddr @blocklist6`. Banning an
  address is an O(1) set-element insert, not a rule insert — this is what
  nftables' own docs recommend once you're past a handful of banned IPs.
- **Auto-expiry via set element timeouts**, not a cron job sweeping rules.
  `block(addr, 10min)` adds the element with `timeout 600s`; the kernel
  removes it on its own.
- **Idempotent setup.** `ensure_infrastructure()` is safe to call on every
  process startup. `add table` / `add chain` / `add set` are naturally
  idempotent in nftables, but `add rule` is *not* — calling it twice
  creates two identical drop rules — so the drop rule is tagged with a
  `comment` and only added if a rule with that comment isn't already
  present in the chain.
- **Injection-safe.** Addresses are validated with `inet_pton` before being
  interpolated into an nft statement string. Anything that isn't a clean
  IPv4/IPv6 address is rejected with `std::invalid_argument` rather than
  reaching the nft parser.
- **Graceful unblock.** Deleting a set element that isn't present fails
  with nftables' `"No such file or directory"`; `unblock()` treats that
  specific case as success rather than propagating it, since the caller's
  actual goal — address not blocked — is already satisfied.

## A libnftables gotcha worth knowing

`nft_ctx_buffer_output()` / `nft_ctx_buffer_error()` must be re-armed
before **every** call to `nft_run_cmd_from_buffer()`, and you must read
`nft_ctx_get_output_buffer()` / `nft_ctx_get_error_buffer()` immediately
after — *without* calling `nft_ctx_unbuffer_output()` /
`nft_ctx_unbuffer_error()` first. Unbuffering tears down the underlying
`open_memstream()` and the buffer reads back empty even on a command that
produced real output or a real diagnostic. This isn't documented in the
header and cost some trial and error to pin down (see
`src/nft_context.cpp`); re-arming before each call correctly resets rather
than appends.

Also worth knowing: not every failure comes with diagnostic text. Parser
errors (bad syntax) always populate the error buffer; some netlink-level
failures (e.g. a rule referencing a table that doesn't exist) currently
don't — `NftError::detail()` will just be empty in that case, only
`NftError::statement()` tells you what was attempted.

## Layout

```
include/nftmgr/nft_error.hpp          exception type, carries nft's own diagnostic text
include/nftmgr/nft_context.hpp        thread-safe RAII wrapper around nft_ctx
include/nftmgr/blocklist_manager.hpp  the fail2ban-style API
src/                                  implementations
src/main.cpp                          CLI
third_party/nlohmann/json.hpp         vendored, header-only, MIT (used to parse `list set` JSON)
```

## Build

Requires `libnftables-dev`, `pkg-config`, `cmake` (>= 3.20), and a
C++23-capable compiler (tested with GCC 13).

```sh
# Debian/Ubuntu/Artix-with-apt-equivalent:
#   pacman -S nftables            (Artix)
#   apt install libnftables-dev pkg-config cmake

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
```

## CLI usage

```
nftmgr init                   create table/chain/sets/rules
nftmgr block <addr> [ttl]     ban an address; ttl like 30, 10m, 2h, 1d (default: permanent)
nftmgr unblock <addr>         lift a ban (no-op if not currently blocked)
nftmgr status <addr>          check whether an address is banned (exit code 0/1)
nftmgr list                   list all banned addresses
nftmgr flush                  lift every ban
```

All operations require `CAP_NET_ADMIN` — run as root, or grant the
capability to the binary / the systemd unit running it
(`AmbientCapabilities=CAP_NET_ADMIN`, `CapabilityBoundingSet=CAP_NET_ADMIN`)
rather than running the whole process as root, if you're wiring this into
something like exossh for SSH-fail-triggered bans.

## Library usage

```cpp
#include "nftmgr/blocklist_manager.hpp"
using namespace std::chrono_literals;

nftmgr::BlocklistManager mgr;   // defaults: inet/nftmgr/input/blocklist4/blocklist6
mgr.ensure_infrastructure();
mgr.block("203.0.113.9", 10min);
mgr.block("2001:db8::1");        // permanent
if (mgr.is_blocked("203.0.113.9")) { /* ... */ }
for (auto& e : mgr.list_blocked()) { /* e.address, e.expires_in */ }
mgr.unblock("203.0.113.9");
```

`NftContext` (the lower-level piece) is also usable on its own if you need
to run arbitrary nft statements outside the blocklist use case — it's just
a thread-safe `run(script)` / `query(script)` pair.

## Not included (by design)

No daemon/log-watching loop is included — that's log-source-specific (you'd
feed it from your SMTP/SSH auth failure handling in vibemail/exossh). This
is deliberately just the nftables maintenance layer underneath one.

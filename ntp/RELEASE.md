# Release Notes — `ntp-v0.1.0`

Tag: `ntp-v0.1.0` — 2026-09-24
Tagger: Randolph Ledesma
Head: `9f78ab1 project(ntp): add system clock and hwclock sync from NTP measurement`

Commits in scope (`5ed5726..ntp-v0.1.0 -- ntp/`):

* `d6e919f project(ntp): develop ntp client implementation`
* `4026085 project(ntp): develop ntp server implementation`
* `9f78ab1 project(ntp): add system clock and hwclock sync from NTP measurement`

Both crates set to `version = "0.1.0"`.

---

## 1. ntp-server v0.1.0

Minimal async NTP (RFC 5905) UDP server in Rust.

Source: `ntp/ntp-server/` @ `4026085`

### What's included

* `src/ntp.rs` (569 lines): packet codec, NTP timestamps (1900 epoch, `NTP_UNIX_EPOCH_DIFF`), encode/decode, unit tests.
* `src/server.rs` (350 lines): request handling, `ServerConfig`, `parse_ref_id`, `secs_to_fixed_16_16`, logging, unit tests.
* `src/main.rs` (112 lines): thin CLI wrapper (`clap` + `tokio` UDP loop, concurrent `handle_request` per datagram, `Ctrl-C` shutdown).
* `src/lib.rs`: crate root `ntp` + `server`.
* `tests/server.rs` (256 lines): 9 integration tests spawning real binary on loopback.

### Protocol behavior

* Listens UDP, replies only to client-mode (mode 3), VN 2-4.
* Response: `LI=0`, `VN=echo(min(req,4))`, `Mode=4`, `Origin=request.Transmit`, `Receive=arrival`, `Transmit=departure`, `Poll=echo(request)`.
* `reference time = server boot time` — stratum-1, local clock is reference, not disciplined upstream.
* 48 bytes minimum; extension bytes ignored. Short/empty/non-client/old-version dropped with log.
* Compatible with `../ntp-module/client.ts` (`0x1B` + 47 zeros).

### CLI

```sh
sudo cargo run -- --bind 0.0.0.0 --port 123
cargo run -- --port 1123 --stratum 1 --ref-id GPS.
cargo run -- --help
```

| Flag | Default |
|---|---|
| `--bind` | `0.0.0.0` |
| `--port` | `123` (use `1123` unprivileged) |
| `--stratum` | `1` (1-15) |
| `--ref-id` | `GPS.` (exactly 4 ASCII) |
| `--precision` | `-20` |
| `--root-delay-ms` | `0.0` |
| `--root-dispersion-ms` | `1.0` |

### Tests

```sh
cargo test # 29 unit + 9 integration
```

Covers: packet round-trips, `0x1B` parsing, extension tolerance, version clamping, client-request matrix, timestamp edges (Unix epoch, pre-1970, fraction), origin-echo, ref-id validation, fixed-point, no-reply paths, v3/v4 replies, timestamp ordering, custom stratum/ref-id/precision/root-*, 20 concurrent clients.

---

## 2. ntp-client v0.1.0

Configurable NTP client in Rust (RFC 5905, 48-byte UDP exchange). Queries server, prints offset/delay, optionally steps system clock + RTC.

Source: `ntp/ntp-client/` @ `d6e919f` + `9f78ab1`

### What's included — `d6e919f`

* `src/ntp.rs` (288 lines): `NtpPacket::new_request(3|4)`, UDP query with DNS resolve, offset `((t2-t1)+(t3-t4))/2`, delay `(t4-t1)-(t3-t2)`, retries/timeout.
* `src/config.rs` (187 lines): layered config CLI > env > TOML > defaults.
* `src/main.rs` (69 lines): text/json output, exit 1 on failure.
* `ntp-client.example.toml`, `.gitignore`, `Cargo.toml` (`clap`, `serde`, `toml`, `chrono`, `anyhow`, `serde_json`).

Side effect in same commit: `ntp/ntp-client/{README,client.ts,package.json,session.txt}` → `ntp/ntp-module/` (Node client preserved).

### What's added — `9f78ab1`

* New `src/clock.rs` (84 lines): `clock_settime(CLOCK_REALTIME)` with EPERM/sudo hint, `hwclock --systohc` RTC sync, `corrected-now (now + offset)` helper.
* New flags (CLI/env/TOML): `--set-system-time`, `--sync-hwclock` (implies set), `--dry-run`, `--max-offset-secs` guard.
* Wired into `main.rs` after NTP query; extended `--print-config`.
* New dep `libc 0.2`; extended `ntp-client.example.toml`; `README.md` +130 lines.
* Verified with dry-run against `127.0.0.1:1123`.

### Config precedence

CLI flags > env vars > TOML file > built-ins. TOML from `--config <path>`, else `$NTP_CLIENT_CONFIG`, else `./ntp-client.toml` (git-ignored; commit only `*.example.toml`).

| Flag / Key | Env | Default |
|---|---|---|
| `--server` | `NTP_SERVER` | `127.0.0.1` |
| `--port` | `NTP_PORT` | `123` |
| `--timeout` | `NTP_TIMEOUT` | `5.0` |
| `--retries` | `NTP_RETRIES` | `2` |
| `--ntp-version 3\|4` | `NTP_VERSION` | `4` |
| `--format text\|json` | `NTP_FORMAT` | `text` |
| `--set-system-time` | `NTP_SET_SYSTEM_TIME` | `false` |
| `--sync-hwclock` | `NTP_SYNC_HWCLOCK` | `false` |
| `--dry-run` | `NTP_DRY_RUN` | `false` |
| `--max-offset-secs` | `NTP_MAX_OFFSET_SECS` | `0.0` (=no limit) |

### Usage

```sh
./target/release/ntp-client --server 127.0.0.1 --port 1123
./target/release/ntp-client --server 127.0.0.1 --port 1123 --format json
./target/release/ntp-client --print-config

# preview sync (no privs needed):
./target/release/ntp-client --server 127.0.0.1 --port 1123 --set-system-time --dry-run
./target/release/ntp-client --server 127.0.0.1 --port 1123 --sync-hwclock --dry-run

# real sync (needs CAP_SYS_TIME):
sudo ./target/release/ntp-client --server 127.0.0.1 --port 1123 --set-system-time
sudo ./target/release/ntp-client --server 127.0.0.1 --port 1123 --sync-hwclock --max-offset-secs 1
```

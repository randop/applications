# ntp-client

Minimal configurable NTP client in Rust (RFC 5905, 48-byte UDP exchange).
Queries an NTP server, prints offset/delay, and can optionally step the
system clock and sync the hardware clock (RTC).

## Features

- NTPv3 / NTPv4 client over UDP
- Offset `((t2-t1)+(t3-t4))/2` and delay `(t4-t1)-(t3-t2)` calculation
- `text` and `json` output
- Layered configuration: CLI > env > TOML > defaults
- Optional system-clock stepping (`clock_settime`) + RTC sync (`hwclock --systohc`)
- `--dry-run` and `--max-offset-secs` safety guard

## Requirements

- Rust 1.98+ (`rustc --version`)
- Linux for clock sync (`CAP_SYS_TIME`, i.e. root/sudo)
- `hwclock` (util-linux) only for `--sync-hwclock`
- A reachable NTP server; local test server in this task: `127.0.0.1:1123`

## Build

```sh
cargo build
cargo build --release
./target/release/ntp-client --help
./target/release/ntp-client --version  # 0.1.0
```

## Configuration

Precedence: CLI flags > environment variables > TOML file > built-in defaults.
TOML is loaded from `--config <path>`, else `$NTP_CLIENT_CONFIG`, else
`./ntp-client.toml` if present. `ntp-client.toml` is git-ignored; commit only
`ntp-client.example.toml`.

| Key / Flag | Env | TOML | Default |
|---|---|---|---|
| `--server` | `NTP_SERVER` | `server` | `127.0.0.1` |
| `--port` | `NTP_PORT` | `port` | `123` |
| `--timeout` (secs, float) | `NTP_TIMEOUT` | `timeout_secs` | `5.0` |
| `--retries` | `NTP_RETRIES` | `retries` | `2` |
| `--ntp-version` (3\|4) | `NTP_VERSION` | `ntp_version` | `4` |
| `--format` (text\|json) | `NTP_FORMAT` | `format` | `text` |
| `--set-system-time` | `NTP_SET_SYSTEM_TIME` | `set_system_time` | `false` |
| `--sync-hwclock` (implies set) | `NTP_SYNC_HWCLOCK` | `sync_hwclock` | `false` |
| `--dry-run` | `NTP_DRY_RUN` | `dry_run` | `false` |
| `--max-offset-secs` (0=no limit) | `NTP_MAX_OFFSET_SECS` | `max_offset_secs` | `0.0` |
| `--config` | `NTP_CLIENT_CONFIG` | — | — |

Boolean flags accept bare form (`--set-system-time` == true) or explicit
(`--set-system-time=false`, `--dry-run=true`).

Inspect the merged config:

```sh
./target/release/ntp-client --print-config
./target/release/ntp-client --config ntp-client.example.toml --print-config
cp ntp-client.example.toml ntp-client.toml
```

## Usage

```sh
# Local test server (standard NTP port is 123)
./target/release/ntp-client --server 127.0.0.1 --port 1123

# JSON output
./target/release/ntp-client --server 127.0.0.1 --port 1123 --format json

# Via env vars
NTP_SERVER=127.0.0.1 NTP_PORT=1123 ./target/release/ntp-client

# Via config file defaults (example file already points at 127.0.0.1:1123)
./target/release/ntp-client
```

Sample text output:

```text
NTP reply from 127.0.0.1:1123 (stratum 1, version 4)
server time : 2026-09-24T09:20:50.657Z
clock offset: +0.000064 s (positive = server ahead)
round-trip delay: 0.000275 s
rtt wall    : 0.000275 s (t4 - t1)
```

## Clock sync (Linux only)

```sh
# Preview only — safe, no privileges needed
./target/release/ntp-client --server 127.0.0.1 --port 1123 --set-system-time --dry-run
./target/release/ntp-client --server 127.0.0.1 --port 1123 --sync-hwclock --dry-run

# Real sync — requires root / CAP_SYS_TIME
sudo ./target/release/ntp-client --server 127.0.0.1 --port 1123 --set-system-time

# System clock + RTC
sudo ./target/release/ntp-client --server 127.0.0.1 --port 1123 --sync-hwclock

# Refuse steps larger than 1s
sudo ./target/release/ntp-client --server 127.0.0.1 --port 1123 --sync-hwclock --max-offset-secs 1
```

Notes:

- Target time is `now + offset`, recomputed just before `clock_settime(CLOCK_REALTIME, ...)`.
- `--sync-hwclock` runs `hwclock --systohc` after stepping the system clock.
- Without privileges `clock_settime` fails with a hint to use sudo.
- Do not run a real step inside unprivileged containers; use `--dry-run`.

## Layout

```text
Cargo.toml                # clap, serde, toml, chrono, anyhow, serde_json, libc
src/main.rs               # CLI entry, output, sync orchestration
src/config.rs             # CLI/env/TOML/default merging
src/ntp.rs                # packet encode/decode, query, offset/delay
src/clock.rs              # clock_settime + hwclock --systohc
ntp-client.example.toml   # tracked template (local test server)
ntp-client.toml           # local override, git-ignored
```

## License

MIT OR Apache-2.0

**Assisted By:** Meta Muse Spark 1.3 contributor-free

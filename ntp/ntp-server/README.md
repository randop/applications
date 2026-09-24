# ntp-server

Minimal async NTP (RFC 5905) UDP server in Rust.

Listens for client-mode (mode 3) requests and replies in server mode
(mode 4), stamping receive/transmit times from the local system clock.
Compatible with `../ntp-client/client.ts` (which sends `0x1B` + 47 zero bytes).

## Layout

```text
ntp-server/
  Cargo.toml
  src/
    lib.rs     # crate root: ntp + server modules
    main.rs    # thin CLI wrapper (clap + tokio UDP loop)
    ntp.rs     # packet codec, NTP timestamps, unit tests
    server.rs  # request handling, config, unit tests
  tests/
    server.rs  # integration tests against the real binary
```

## Requirements

- Rust stable (edition 2021)
- A C linker (`cc`) for final linking, e.g. `gcc` / `clang`:
  `cargo build` / `cargo test` fail with `linker 'cc' not found` without one.

## Run

```sh
# Standard NTP port (needs root / CAP_NET_BIND_SERVICE)
sudo cargo run -- --bind 0.0.0.0 --port 123

# Unprivileged (for dev / containers without a linker workaround)
cargo run -- --port 1123 --stratum 1 --ref-id GPS.

# All options
cargo run -- --help
```

| Flag | Default | Notes |
| ---- | ------- | ----- |
| `--bind` | `0.0.0.0` | bind address (v4/v6) |
| `--port` | `123` | use `1123` unprivileged |
| `--stratum` | `1` | 1–15 |
| `--ref-id` | `GPS.` | exactly 4 ASCII chars |
| `--precision` | `-20` | log2 seconds |
| `--root-delay-ms` | `0.0` | advertised |
| `--root-dispersion-ms` | `1.0` | advertised |

## Test

```sh
cargo test      # 29 unit + 9 integration (spawns real binary on loopback)
cargo fmt --check
```

Unit tests (`src/ntp.rs`, `src/server.rs`): packet round-trips, `0x1B`
parsing, extension-field tolerance, version clamping, client-request
matrix, timestamp edge cases (Unix epoch, pre-1970, fraction encoding),
origin-echo, ref-id validation, fixed-point conversion, no-reply paths.

Integration tests (`tests/server.rs`): boot the real binary per test and
speak NTP over UDP — v3/v4 replies, timestamp ordering/sanity, short /
empty / non-client / old-version packets dropped, custom
`--stratum/--ref-id/--precision/--root-*-ms` advertised, 20 concurrent
clients.

Manual check against the Node client: point it at
`127.0.0.1:1123` and confirm `Mode: 4`, `Stratum: 1`.

## Protocol notes (RFC 5905)

- Packet is 48 bytes minimum; bytes after 48 (extension fields) are ignored.
- Only VN 2–4, mode 3 (client) packets are answered.
- Response: `LI=0`, `VN=echo(min(req,4))`, `Mode=4`,
  `Origin=request.Transmit`, `Receive=arrival`, `Transmit=departure`,
  `Poll=echo(request)`, reference time = server boot time (stratum-1,
  local clock is the reference — not disciplined to upstream).
- Short (<48B) or non-client packets are logged and dropped.

## References

- <https://www.meinbergglobal.com/english/info/ntp-packet.htm>
- RFC 5905 (NTPv4)

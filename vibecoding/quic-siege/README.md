# quic-siege

Raw-QUIC (non-HTTP) connection and stream load generator, built directly on
`liblsquic`'s public API. No HTTP/3 layer, no libevent -- one `lsquic_engine`
+ one `epoll` reactor per worker thread, one connected UDP socket per QUIC
connection.

Verified against lsquic v4.9.3 headers/source (function signatures, struct
fields, callback timing) -- see design notes below. Not yet linked against a
built `liblsquic.so`; that step needs BoringSSL or OpenSSL >=3.5, which this
build environment didn't have, so do a build-and-fix pass on your machine
before trusting it against a real server.

## Build

### 1. Build liblsquic

lsquic needs a QUIC-capable TLS library: either Google's BoringSSL (the path
lsquic's own CI uses) or OpenSSL >=3.5 (has `SSL_set_quic_tls_cbs` natively).
Check what you've got first:

```sh
openssl version
```

If it's 3.5+, skip straight to the OpenSSL branch below -- much less to
build.

**BoringSSL branch** (needs `go`, `cmake`, `ninja` as build-time tools only):

```sh
git clone https://boringssl.googlesource.com/boringssl
cmake -B boringssl/build -S boringssl -GNinja -DCMAKE_BUILD_TYPE=Release
ninja -C boringssl/build

git clone --recursive https://github.com/litespeedtech/lsquic
cmake -B lsquic/build -S lsquic \
  -DBORINGSSL_DIR="$PWD/boringssl" \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DCMAKE_BUILD_TYPE=Release
cmake --build lsquic/build -j"$(nproc)"
sudo cmake --install lsquic/build
```

**OpenSSL >=3.5 branch:**

```sh
git clone --recursive https://github.com/litespeedtech/lsquic
cmake -B lsquic/build -S lsquic \
  -DLSQUIC_SSL_LIB=OPENSSL \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DCMAKE_BUILD_TYPE=Release
cmake --build lsquic/build -j"$(nproc)"
sudo cmake --install lsquic/build
```

Check `lsquic/CMakeLists.txt` in whichever version you pull -- the exact
CMake option names/defaults drift between releases.

### 2. Build quic-siege

```sh
make                          # if lsquic installed to /usr/local
make LSQUIC_PREFIX=/opt/lsquic  # if installed elsewhere
```

Raw binary, no runtime deps beyond `liblsquic.so` + its own deps
(`libssl`/`libcrypto` or BoringSSL's `.so`s, `libz`). No systemd units, no
container image -- copy the binary wherever it needs to run.

## Usage

Pick exactly one stop condition, `-n` (fixed count) or `-t` (duration):

```sh
# 10k connections total, 200 in flight at once, default 1 stream + 1KB payload each
./quic-siege -H quic.example.com -p 4433 -A raw -n 10000 -c 200

# sustained 60s run at a capped connect rate, 4 streams/conn, wait for echo response
./quic-siege -H 10.0.0.5 -p 4433 -A echo -t 60 -c 500 -r 300 -s 4 -w -b 4096

# verify the peer cert instead of the default no-verify (most test/siege targets are self-signed)
./quic-siege -H quic.example.com -p 4433 --verify-cert -t 30
```

`--alpn` has to match whatever ALPN the target server's listener is
registered under -- there's no protocol auto-negotiation to fall back on.
Fire-and-forget mode (default, no `-w`) measures connection + stream *setup*
capacity: write the payload, half/full-close, move on. `-w` additionally
waits for the peer to send a response and EOF the stream, and reports that
as a second latency distribution -- use it if the target actually echoes or
answers on the stream; against a server that only accepts and resets
unknown-ALPN streams, `-w` will just record a pile of `streams_failed`.

Run `./quic-siege -h` for the full flag list (concurrency, threads, rate,
idle/handshake timeouts, drain grace period, etc).

## Design notes

- **One UDP socket per connection**, `connect()`-ed to the target. Sidesteps
  `IP_PKTINFO`/ECN cmsg bookkeeping a shared socket would need -- the kernel
  handles source-address selection and demuxes replies. Costs one fd per
  in-flight connection; raise `RLIMIT_NOFILE` if concurrency needs to go past
  a few thousand per process.
- **Per-connection context** is threaded through via `peer_ctx` passed to
  `lsquic_engine_connect()`, retrieved inside `on_new_conn` via
  `lsquic_conn_get_peer_ctx(c, NULL)`. `on_new_conn` fires *synchronously*
  inside `lsquic_engine_connect()` (confirmed in `lsquic_engine.c`), so
  there's no race between issuing the connect and the callback seeing the
  right socket.
- **Cert verification defaults off.** Not supplying `ea_get_ssl_ctx` makes
  lsquic build its own permissive per-connection `SSL_CTX` -- the right
  default for a load-testing tool aimed at test/staging targets. `--verify-cert`
  swaps in a real `SSL_CTX` with `SSL_CTX_set_verify(SSL_VERIFY_PEER, ...)`
  against system CA paths.
- **Streams open eagerly.** All `--streams` calls to `lsquic_conn_make_stream()`
  happen up front in `on_new_conn`; lsquic queues any that exceed the peer's
  advertised concurrent-stream limit and dispatches them as slots free
  (`lsquic_conn_n_pending_streams()`), so this is safe regardless of `-s`.
- **Shutdown is graceful with a hard floor.** Reaching `-n`/`-t` (or ^C) stops
  new connects; in-flight streams/connections are given `--drain-timeout`
  (or 1s on ^C) to finish naturally, then stragglers are force-closed via
  `lsquic_conn_close()`, then a final +5s safety valve force-exits the
  worker loop regardless, in case some connection's teardown callback never
  fires.
- **Latency percentiles use reservoir sampling** once a worker's sample
  buffer (1M entries/metric) fills, so long high-rate runs stay statistically
  representative instead of just reporting the first few seconds.

## What's not implemented

- No HTTP/3 framing -- by design, this targets the raw QUIC transport, not
  an HTTP/3 endpoint. (If you actually need that, `lsquic`'s own
  `bin/http_client.c` + `bin/perf_client.c` examples are a better starting
  point than bolting nghttp3 onto this.)
- No 0-RTT / session resumption.
- No connection migration / multi-path.
- No QUIC datagram (RFC 9221) support.

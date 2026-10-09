# websrv

`websrv` is a Linux-only, QUIC-first Rust web server. Its runtime is **Monoio's `IoUringDriver`**, selected explicitly at startup. HTTP/3 over QUIC/UDP is the primary protocol and is implemented with `quiche`. Optional HTTPS/1.1 over TCP is provided by a small rustls-backed HTTP/1.1 parser; TCP stream I/O still uses the same Monoio `IoUringDriver`. Both transports share virtual-host routing, static sites, and Rust REST controllers. HTTPS/1.1 responses advertise the QUIC endpoint through `Alt-Svc` so compatible clients can upgrade to HTTP/3.

**There is no Tokio dependency, no epoll/legacy driver feature, and no request-time I/O backend fallback.** If `io_uring_setup` is unavailable or denied by a container seccomp policy, startup fails instead of silently changing drivers. Request-time UDP/TCP socket operations and static-content reads are driven by Monoio’s mandatory `IoUringDriver`. Configuration parsing, path canonicalization, and initial TLS PEM loading are bootstrap operations performed before traffic is accepted. `tcp_http1_enabled: true` enables TCP/HTTPS/1.1 on the same numeric address/port as the UDP/QUIC listener; set it to `false` for HTTP/3-only operation. This is a *protocol* fallback, not an I/O backend fallback.

## Architecture

```text
UDP socket (Monoio IoUringDriver)             TCP socket (Monoio IoUringDriver)
             │                                               │
             ▼                                               ▼
       quiche QUIC + TLS                               rustls TLS + HTTP/1.1
             │                                               │
             ▼                                               │
       quiche HTTP/3                                        │
             └──────────────────────┬────────────────────────┘
                                    ▼
                       authority / Host selection
       ┌─────┴─────────┐
       ▼               ▼
 static-site module  Rust controller registry
       │               │
 Monoio file reads    async Rust handlers
   through ring       (health/status/echo/items)
```

- **Runtime:** `monoio` is built with `default-features = false` and the `iouring` + `utils` features; `utils` provides runtime utilities and does not enable a legacy I/O driver. The code constructs `RuntimeBuilder<IoUringDriver>` explicitly. Monoio is a thread-per-core runtime built around io_uring rather than a Tokio compatibility layer. See [Monoio's runtime documentation](https://docs.rs/monoio/latest/monoio/).
- **QUIC/HTTP/3:** `quiche` owns QUIC, TLS handshake, loss recovery, congestion control, HTTP/3 framing, and QPACK. Its API leaves UDP socket driving and the event loop to the application; websrv drives those on Monoio. See [quiche HTTP/3 documentation](https://docs.rs/quiche/latest/quiche/h3/).
- **HTTP/1.1 protocol fallback:** rustls handles TLS cryptography; HTTP/1.1 parsing and request/response framing are in `src/http1_server.rs`. Monoio performs TCP accept/read/write operations. The small HTTP/1.1 implementation accepts `Content-Length` request bodies, rejects `Transfer-Encoding` and `Expect`, limits header/body sizes, and supports persistent connections. It is not an HTTP/2 implementation.
- **Virtual hosting:** exact names are checked before wildcard names; aliases are supported. Each host has exactly one serving target: a static root or a named Rust controller table.
- **Controller model:** controller routes are declared in YAML and map to Rust handler modules. No shell process, CGI, FastCGI daemon, or arbitrary configured executable is launched. Add business logic under `src/controllers/` and register its route handler explicitly.
- **File reads:** static content is opened and read asynchronously using Monoio's file API. Static content roots should be owned by the publisher and not writable by the websrv runtime user. Do not place untrusted symlinks under a served root.
- **Startup-only operations:** config parsing, TLS key loading, root canonicalization, and tracing initialization happen during bootstrap before serving traffic. Request-time UDP and TCP socket I/O plus static file reads use the mandatory io_uring runtime.

## Requirements

- Linux with a kernel and configuration that permit `io_uring` (typically Linux 5.6+; a newer maintained kernel is recommended).
- A container/runtime seccomp policy that permits `io_uring_setup`, `io_uring_enter`, and the required registration calls.
- Rust stable and Cargo. The `quiche` build may need a C/C++ toolchain, CMake, Clang, Perl, and pkg-config to build its TLS dependency.
- A trusted certificate chain and matching private key in PEM format, covering every hostname you serve. The same certificate/key pair is used by QUIC and HTTPS/1.1.
- UDP ingress for HTTP/3 and, when enabled, TCP ingress for HTTPS/1.1 on the same numeric port. The default is `0.0.0.0:8443` for both protocols.

## Build and run

```bash
mkdir -p certs
# Install a trusted certificate chain at certs/fullchain.pem and its key at certs/privkey.pem.
# Edit virtual_hosts and controllers in config.yaml.
cargo fmt --all
cargo test
cargo build --release
./target/release/websrv --config ./config.yaml
```

The default config path is `config.yaml` in the current working directory. Use `-c` or `--config` to specify another file. Relative certificate and static-root paths are resolved against the directory containing the YAML file. Environment variables do not override YAML settings and `.env` files are not loaded.

### Local HTTP/3 smoke test

The hostname in the `Host`/`:authority` header must match a configured virtual host. For local-only testing, create a development certificate with SANs for `example.test`, `api.example.test`, and any tenant names. Use a curl build that supports HTTP/3 for the primary protocol:

```bash
curl --http3-only --resolve example.test:8443:127.0.0.1 \
  --cacert certs/fullchain.pem https://example.test:8443/

curl --http3-only --resolve api.example.test:8443:127.0.0.1 \
  --cacert certs/fullchain.pem https://api.example.test:8443/healthz
```

The TCP protocol fallback shares the same certificate and hostname routing. To smoke-test it, use a curl build supporting standard TLS:

```bash
curl --http1.1 --resolve example.test:8443:127.0.0.1 \
  --cacert certs/fullchain.pem https://example.test:8443/

curl --http1.1 --resolve api.example.test:8443:127.0.0.1 \
  --cacert certs/fullchain.pem https://api.example.test:8443/healthz
```

For a trusted local development CA, add the CA certificate to `--cacert`; do not expose development certificates to public traffic.

## `config.yaml`

The shipped config is executable documentation. This is the high-level shape:

```yaml
listen: "0.0.0.0:8443"
tcp_http1_enabled: true
tls_cert: "certs/fullchain.pem"
tls_key: "certs/privkey.pem"
max_request_body_bytes: 1048576
max_static_file_bytes: 16777216
log_filter: "info,websrv=debug"
default_host: "example.test"

virtual_hosts:
  - host: "example.test"
    aliases: ["www.example.test"]
    static:
      root: "public"
      index: "index.html"
      spa_fallback: false
      cache_control: "public, max-age=60"
  - host: "*.tenant.example.test"
    static:
      root: "sites/tenant"
      index: "index.html"
      spa_fallback: true
      cache_control: "public, max-age=300"
  - host: "api.example.test"
    controller: "public_api"

controllers:
  public_api:
    routes:
      - { method: "GET", path: "/healthz", handler: "health" }
      - { method: "GET", path: "/status", handler: "status" }
      - { method: "POST", path: "/echo", handler: "echo" }
      - { method: "GET", path: "/v1/items", handler: "items" }
      - { method: "POST", path: "/v1/items", handler: "items" }
      - { method: "GET", path: "/v1/items/{id}", handler: "items" }
      - { method: "PUT", path: "/v1/items/{id}", handler: "items" }
      - { method: "PATCH", path: "/v1/items/{id}", handler: "items" }
      - { method: "DELETE", path: "/v1/items/{id}", handler: "items" }
```

| Key | Meaning |
|---|---|
| `listen` | Socket address used for the HTTP/3 UDP listener and, if enabled, the HTTP/1.1 TCP listener |
| `tcp_http1_enabled` | Enables the HTTPS/1.1 TCP protocol fallback; does not enable a non-io_uring runtime |
| `tls_cert`, `tls_key` | PEM certificate chain and private key shared by QUIC TLS and rustls; certificate SANs must cover configured hosts |
| `max_request_body_bytes` | Request payload upper bound; validated at collection and dispatch, with a 16 MiB hard maximum |
| `max_static_file_bytes` | Maximum bytes read for one static file; validation caps it at 32 MiB |
| `default_host` | Optional exact host (or alias) served for otherwise-unmatched authorities |
| `virtual_hosts[].host` | Exact host or wildcard, e.g. `*.example.com` |
| `virtual_hosts[].aliases` | Additional exact or wildcard host patterns |
| `virtual_hosts[].static.root` | Filesystem root for the static website |
| `virtual_hosts[].static.index` | Single filename served for `/` and directory-like paths |
| `virtual_hosts[].static.spa_fallback` | Serve the index page for unknown extension-less paths |
| `virtual_hosts[].controller` | Name of the in-process Rust controller route table |
| `controllers.<name>.routes[]` | Method + path pattern + registered Rust handler name |

Unknown YAML keys are rejected. A virtual host must configure exactly one of `static` or `controller`. Static path traversal attempts and encoded `..` segments are rejected. Keep served roots immutable to the server process; this project assumes trusted deployment content and does not claim a defense against a privileged local writer racing filesystem changes.

## Built-in REST handlers

The example controller registry includes:

- `health`: JSON health response.
- `status`: build/runtime feature status.
- `echo`: JSON/text request echo with the configured request-body cap.
- `items`: demonstration CRUD handler backed by an in-memory map (`GET`, `POST`, `PUT`, `PATCH`, `DELETE`). This data is **not persistent** and is intended as a controller integration example, not a database replacement.

Controllers live in `src/controllers/`. The dispatcher receives a parsed HTTP/3 or HTTP/1.1 request, matches method and route parameters, and invokes Rust functions as Monoio tasks. To add an application module, implement the handler, add its route name to the explicit registry and configuration validation, then declare routes under `controllers`.

## Docker and systemd

Docker image builds include a C toolchain for quiche and expose **UDP 8443 and TCP 8443**. Mount a certificate and key at `/run/secrets/tls_cert` and `/run/secrets/tls_key`, and make sure the container runtime's seccomp policy permits io_uring. Do not use `--privileged` as a workaround; use an explicit, reviewed seccomp profile.

The systemd unit runs as an unprivileged `websrv` user. Install it and the binary under `/opt/websrv`, edit `/opt/websrv/config.yaml`, and ensure the service user can read the private key and static roots.

## Security and production readiness

- Use valid TLS certificates and automate rotation; the server does not generate certificates. The configured certificate and private key are shared by QUIC and TCP TLS and must cover every configured hostname.
- Expose UDP for HTTP/3 and TCP for HTTPS/1.1 when `tcp_http1_enabled` is true. Both listeners use the mandatory io_uring runtime; disable the TCP protocol fallback with `tcp_http1_enabled: false` if HTTP/3-only deployment is required.
- Keep private keys readable only by the service account.
- Do not allow the serving account to modify website roots; avoid symlinks inside served roots.
- Configure edge rate limiting / DDoS protection and observe structured logs before exposing a public endpoint. Active connection migration is disabled in this version; connections are keyed by destination connection ID to allow multiple connections from one client UDP socket without conflation.
- Audit controller handlers before enabling mutating API routes. The sample item store is in-memory and has no authentication or authorization built in.
- Set resource limits, benchmark under representative traffic, and test overload behavior before production deployment.

## Validation status

This implementation has not been compiled or load-tested in the current environment because Rust/Cargo were not available when the files were generated. The CI workflow runs formatting, `cargo check`, tests, and Clippy on Linux; passing those checks plus HTTP/3 and HTTPS/1.1 interoperability tests is a release gate, not an assumption.

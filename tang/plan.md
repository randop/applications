# Tang

Source project: https://github.com/latchset/tang

Rust Rewrite Plan

## Assumptions to confirm

- **Library constraint.** "Avoid a library that mimics its functions" is read as: no existing Tang implementation, and no general-purpose JOSE crate that is a port of libjose. Third-party crates are limited to primitives (curve arithmetic, hashing, RNG, JSON, base64). The upstream Tang source and libjose serve as behavioral references only.
- **Source access.** The upstream C source tree could not be fetched, so this plan rests on the README's protocol description. Items marked **[verify]** must be confirmed against `src/` and libjose during Phase 0.

## What is being replaced

Tang exposes `GET /adv`, `GET /adv/{kid}` and `POST /rec/{kid}`. The advertisement is a JWS-signed JWK set, and recovery is a McCallum-Relyea exchange where the client blinds its key with an ephemeral key. The server computes `y = x·S` and the client recovers the key as `y − z`. The server is stateless and runs either under socket activation (systemd, xinetd) or standalone as of v12. Key rotation works by hiding old key files with a leading dot, so rotation semantics depend on directory scanning.

## Phase 0: Behavior capture

- Read `src/`, `doc/`, `units/` and `tests/` and write a behavior spec covering routes, content types, status codes, request and header limits, logging, and the exact selection rules for signing vs. exchange keys **[verify]**.
- From libjose, take only the behavior Tang depends on: RFC 7638 thumbprint, the ECMR algorithm, `key_ops` conventions, and the JWS serialization used in `/adv` **[verify]**.
- Run upstream `tangd` against fixed fixture keys and record golden transcripts of `/adv`, `/adv/{kid}` and `/rec/{kid}`, including error cases.
- **Decide the license.** Tang is GPL-3.0, and a port written from its source may be a derivative work. Settle this before writing code, and choose between a GPL-compatible license and a clean-room process (legal advice recommended).

## Phase 1: Architecture

A Cargo workspace with these crates:

| Crate | Responsibility |
|---|---|
| `tang-jose` (internal, minimal) | base64url, EC JWK model, RFC 7638 thumbprint, JWS sign/verify. Only what Tang needs, not a general JOSE library. |
| `tang-core` | Key store, advertisement builder, recovery operation. Pure logic with no I/O. |
| `tangd` | HTTP front-end and runtime modes. |
| `tang-cli` | Key generation, key display, rotation. |
| `tang-testkit` | Independent reference client (provisioning and recovery) for tests. |

**Primitives:** RustCrypto `p521` / `p384` / `p256` (point arithmetic and ECDSA), `sha2`, `subtle`, `zeroize`, `getrandom`, `base64ct`, `serde_json`.

**Key risk:** ECMR defaults to P-521, so `p521` point arithmetic maturity must be proven in a spike, including point addition, subtraction and constant-time scalar multiplication **[verify]**. Hand-written curve arithmetic is off the table, so the fallback would be a different audited backend.

**HTTP:** Use a small HTTP/1.1 layer over `httparse` (a parser, not a Tang imitation), shared by the stdin/stdout mode and the TCP mode. Tang's traffic is two tiny endpoints, so this keeps the attack surface smaller than pulling in a full framework. Confirm in the spike that it handles upstream clients' quirks.

## Phase 2: Core protocol

1. **Key store.** Load `*.jwk` from a configurable directory (upstream default `/var/db/tang`) and skip dot-prefixed files. Classify keys by `alg` / `key_ops`, with the kid being the thumbprint. Preserve the "rename to hide" rotation behavior; the simplest correct approach is to rescan on each request.
2. **Advertisement.** Build the public JWK set and sign it with all signing keys, or only the requested one for `/adv/{kid}`. Exact JWS shape and `Content-Type` need confirming **[verify]**.
3. **Recovery.** Parse the POSTed JWK, check that its curve matches the key, validate that the point is on the curve and not the identity, compute `S·x`, and return it as a JWK. The server never learns the client key.
4. **Kid handling.** Match the kid against loaded thumbprints and never use it as a filename, which avoids path traversal.

## Phase 3: Runtime and operations

- **Socket-activation / inetd mode:** serve one connection on stdin/stdout, plus systemd `LISTEN_FDS` handling. Keep the xinetd wrapper usable.
- **Standalone mode:** listener, connection and header limits, read timeouts, graceful shutdown.
- **CLI parity:** key generation on first start, key display, and rotation as a command that automates the README's three steps. Keep the existing key-file format so the Rust daemon can replace the C one in place.
- **Hardening:** `#![forbid(unsafe_code)]` except one isolated fd-adoption module, zeroized private scalars, key-file permission checks, no key material in logs, request-size caps, and sandboxing in the systemd units.

## Phase 4: Verification

- **Standards vectors:** RFC 7638 thumbprints and RFC 7515 ES512 signing examples.
- **Property tests:** random-key provisioning/recovery round trips, including the `K = y − z` identity.
- **Interop (the real acceptance gate):**
  - Rust server against upstream clients (`jose` CLI, Clevis Tang pin).
  - Rust testkit client against upstream `tangd`.
  - Golden-transcript diffs from Phase 0.
- **Ported black-box suite:** adapt upstream's curl/socat tests as conformance tests run against both implementations.
- **Negative tests:** invalid-curve points, wrong curve, identity point, oversized or malformed bodies, unknown kid, wrong method or route.
- **Fuzzing:** `cargo-fuzz` targets for the HTTP parser, JWK parser and router.
- **CI:** clippy, `cargo-deny`, `cargo-audit`, and a build matrix covering the platforms upstream supports (Linux, FreeBSD, OpenWrt-class targets via static musl).

## Phase 5: Packaging and cutover

- Man pages (upstream docs are AsciiDoc), systemd unit files, and distro packaging parity.
- Run side by side with the C daemon against the same key directory, then switch over.

## Open decisions

1. License path (see Phase 0).
2. Supported algorithm matrix beyond ES512 and P-521 ECMR. Upstream advertises whatever is in the key directory, so supporting other curves may be needed for compatibility **[verify]**.
3. FIPS requirements. RustCrypto is not FIPS-validated, which matters for RHEL-style deployments and could change the backend choice.
4. Whether standalone mode caches keys or rescans per request.

**Assisted By:** Anthropic Claude Sonnet 5.5

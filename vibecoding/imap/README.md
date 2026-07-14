# Barebones IMAP Server (Seastar v25.05)

RFC 3501 **IMAP4rev1**-oriented server skeleton built on **Seastar v25.05** with **C++23 coroutines**.

Seastar v25.05 compliant — coroutines + core idioms (`do_with`, `with_gate`, `abort_source`, `sharded`, `maybe_yield`, `lw_shared_ptr`).

## Scope

Barebones but protocol-shaped:

| Area | Support |
|------|---------|
| Greeting / tagged commands / CRLF lines | Yes |
| States: not authenticated → authenticated → selected → logout | Yes |
| CAPABILITY, NOOP, LOGOUT, LOGIN | Yes |
| SELECT / EXAMINE, LIST, STATUS, CREATE, DELETE, RENAME | Yes |
| CLOSE, EXPUNGE, CHECK, SEARCH (basic), FETCH, STORE, COPY, UID | Yes |
| APPEND (simple literal) | Yes |
| IDLE / STARTTLS / SASL beyond LOGIN | Not implemented (advertised only where noted) |
| Persistence | In-memory only (demo mailboxes + sample messages) |

Not a production MTA/MDA. Suitable for learning Seastar networking and IMAP framing.

## Default listen

- Host: `0.0.0.0`
- Port: `1143` (non-privileged; real IMAP is `143` / `993`)

## Demo credentials

| User | Password |
|------|----------|
| `demo` | `demo` |
| `alice` | `secret` |

Each successful login gets a shard-local INBOX (and a few demo folders) with sample messages.

## Build notes (do not run here per project constraint)

Requires Seastar **v25.05** installed and discoverable via CMake `find_package(Seastar)`.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/imap-server --port 1143
```

Compile flags: `-std=c++23`. Link with Seastar as provided by `Seastar::seastar`.

Seastar reactor options (CPU pinning, etc.) are available via the usual `app-template` flags.

## Architecture

```
main.cc              app_template, sharded service lifecycle
server.{hh,cc}       listen/accept loop, abort_source + gate
connection.{hh,cc}   per-socket IMAP session state machine
protocol/            line + literal parser, response writers
mailbox/             in-memory messages, flags, UID/sequence map
auth/                trivial password check
```

### Seastar idioms used

- **`sharded<imap_server>`** — one accept path per shard
- **`gate` + `with_gate`** — connection work tracked; `co_await _gate.close()` on stop
- **`abort_source`** — stop accept; connection loops check abort
- **`coroutine::maybe_yield()`** — CPU-bound SEARCH/FETCH result formatting
- **`lw_shared_ptr`** — shard-local session / mailbox handles only
- **I/O streams** — `flush()` before `close()` on output

## Quick manual test

```text
$ nc 127.0.0.1 1143
* OK [CAPABILITY IMAP4rev1 AUTH=PLAIN] IMAP ready
a1 LOGIN demo demo
a1 OK LOGIN completed
a2 SELECT INBOX
* FLAGS (\Answered \Flagged \Deleted \Seen \Draft)
* 3 EXISTS
* 0 RECENT
* OK [UIDVALIDITY 1] UIDs valid
* OK [UIDNEXT 4] Predicted next UID
* OK [READ-WRITE] SELECT completed
a2 OK [READ-WRITE] SELECT completed
a3 FETCH 1 (FLAGS UID ENVELOPE)
...
a4 LOGOUT
* BYE Logging out
a4 OK LOGOUT completed
```

## RFC notes

Implements a useful subset of [RFC 3501](https://datatracker.ietf.org/doc/html/rfc3501) command syntax and response forms. Untagged `*` responses and tagged completion codes follow common client expectations (Thunderbird / mutt style). Literal upload for APPEND uses the `{n}` / `+` continuation form.

## License

Demo / educational code; no warranty.

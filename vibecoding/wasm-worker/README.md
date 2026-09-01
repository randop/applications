# Currency Conversion GraphQL API — Cloudflare Workers + WebAssembly + Turso

A GraphQL currency-conversion service that runs on Cloudflare Workers. Rate
data lives in [Turso](https://turso.tech) (distributed SQLite/libSQL); the
actual conversion arithmetic runs in a small **C++ module compiled to
WebAssembly**, so money math never touches IEEE‑754 floats.

## Architecture

```
                 ┌─────────────────────────────────────────────┐
  HTTP POST      │  Cloudflare Worker (src/worker.js, JS/V8)    │
  /graphql   ───▶│   - routes & GraphiQL UI                     │
                 │   - graphql-js executes the schema           │
                 │   - resolvers (src/graphql/schema.js)        │
                 │        │                        │            │
                 │        ▼                        ▼            │
                 │  TursoClient               CurrencyEngine    │
                 │  (src/db/turso.js)         (src/wasm/*.js)   │
                 │        │                        │            │
                 └────────┼────────────────────────┼────────────┘
                           │ fetch() HTTP                │ calls into
                           ▼                              ▼
                 Turso `/v2/pipeline`          wasm/currency.wasm
                 (Hrana-over-HTTP)             (C++, fixed-point math)
```

**Why HTTP, not a native DB driver?** Cloudflare Workers' sandbox has no raw
TCP socket access from WASM/JS — only `fetch`. Turso exposes exactly such an
HTTP API (the Hrana `v2/pipeline` protocol), so `src/db/turso.js` talks to it
directly with `fetch`, with no extra client dependency.

**Why a C++/WASM module for the math?** Two reasons, not just novelty:
1. **Correctness.** Currency math done in floats is a classic source of bugs
   (`0.1 + 0.2 !== 0.3`). `wasm/currency.cpp` represents every amount and
   rate as a scaled 64-bit integer and does the one multiply/divide it needs
   with a hand-written 128-bit-precision routine (`mul_div_u64`), so results
   are exact and reproducible — no `__int128`/compiler-rt dependency, so it
   links cleanly with `-nostdlib`.
2. **Rate-graph resolution.** The engine resolves a requested pair via a
   direct rate, that rate's inverse, or a bridge through USD, entirely in
   compiled WASM, and exposes a `batch_convert` entry point so converting to
   several target currencies costs one host↔wasm call instead of N.

The module is freestanding (no libc/libc++), talks to JS only through its own
linear memory (a small bump allocator, see `alloc`/`reset_arena` in
`currency.cpp`), and is ~2 KB compiled.

## Project layout

```
wasm/currency.cpp        C++ conversion engine (direct/inverse/bridge, fixed-point)
wasm/build.sh             clang++ → wasm32 build script
wasm/currency.wasm        prebuilt binary (rebuild with `npm run build:wasm`)
src/worker.js              Worker entry point: routing, GraphiQL, GraphQL execution
src/graphql/schema.js       GraphQL SDL + resolvers (graphql-js)
src/db/turso.js             Turso HTTP (v2/pipeline) client
src/wasm/currency.js        JS wrapper marshalling data across the wasm boundary
migrations/0001_init.sql    Turso schema + seed currencies/rates
tests/wasm.test.mjs         Direct tests of the compiled wasm module
tests/resolvers.test.mjs    Full GraphQL/resolver/wasm integration test (mocked Turso)
wrangler.toml                Cloudflare Workers deployment config
```

## Setup

### 1. Create the Turso database

```bash
turso db create currency-api
turso db shell currency-api < migrations/0001_init.sql
turso db show currency-api --url        # -> TURSO_DATABASE_URL
turso db tokens create currency-api     # -> TURSO_AUTH_TOKEN
```

### 2. Install dependencies

```bash
npm install
```

Compiling the WASM module requires `clang`/`clang++` with `lld` (the
wasm32 target ships with a stock LLVM install, no extra SDK needed):

```bash
# Debian/Ubuntu
sudo apt-get install clang lld
```

A prebuilt `wasm/currency.wasm` is included, so this is only needed if you
change `wasm/currency.cpp`.

### 3. Configure

Edit `wrangler.toml`:

```toml
[vars]
TURSO_DATABASE_URL = "https://your-db-org.turso.io"
```

Set the auth token as a secret (never commit it):

```bash
npx wrangler secret put TURSO_AUTH_TOKEN
```

### 4. Run tests

```bash
npm test
```

This runs `tests/wasm.test.mjs` (exercises the compiled module directly in
Node's WASM engine — the same engine Workers uses) and
`tests/resolvers.test.mjs` (runs the real GraphQL schema + resolvers + wasm
engine against a mocked Turso HTTP endpoint, end to end).

### 5. Develop / deploy

```bash
npm run dev       # wrangler dev — local server with GraphiQL at http://localhost:8787
npm run deploy    # wrangler deploy
```

Both scripts rebuild `wasm/currency.wasm` first.

## Example queries

```graphql
query Convert {
  convert(amount: 100, from: "USD", to: "EUR") {
    result
    method       # DIRECT | INVERSE | BRIDGE
    effectiveRate
    to { code symbol }
  }
}

query Fanout {
  convertToMany(amount: 250, from: "USD", to: ["EUR", "GBP", "JPY"]) {
    to { code }
    result
    method
  }
}

mutation RecordRate {
  setRate(base: "USD", quote: "CHF", rate: 0.88) {
    rate
    fetchedAt
  }
}

query History {
  rateHistory(base: "USD", quote: "EUR", limit: 5) {
    rate
    fetchedAt
  }
}
```

`GET /` (or `/graphiql`) serves an interactive GraphiQL UI pointed at
`/graphql`. `GET /healthz` is a plain liveness check.

## Notes / things to adapt for production

- **Rate ingestion**: `setRate` is append-only (full history is kept); wire
  up a scheduled Worker (Cron Trigger) that calls it periodically from a real
  FX data provider.
- **USD as bridge**: the engine bridges through whatever currency index you
  pass as `bridgeIdx`; the resolvers hardcode `"USD"`. Swap that if your rate
  data is anchored to a different base.
- **Auth**: this schema has no authentication layer; put it behind Cloudflare
  Access, an API-key check in `src/worker.js`, or similar before exposing
  mutations publicly.
- **Bundle size**: the deployed Worker bundles `graphql` (~170 KB gzipped)
  plus the ~2 KB wasm module — comfortably inside Workers' limits, but worth
  knowing if you're optimizing cold-start size further.

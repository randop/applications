# exchange-rate-app

Fetches live exchange rates with **undici** and converts a base currency to a
target currency — defaults to **USD → PHP**. Compiled to a native binary with
**scriptc** (Vercel Labs' TypeScript-to-native compiler).

## Why `--dynamic`

scriptc compiles plain, statically-typed TypeScript straight to native code.
`undici` is a real npm package with untyped, V8-oriented internals, so it
can't be compiled statically — it needs to run in scriptc's embedded
**dynamic island** (quickjs-ng, ~620KB), opted into with the `--dynamic` flag.
Your own code (`main`, `getExchangeRate`, `argOrDefault`) still compiles to
native code; only the `undici` call crosses into the island, and its result
is validated as it crosses back. See https://scriptc.dev/dependencies.

## Setup

```bash
npm install
```

Requires Node.js 24+ and clang for scriptc itself:

```bash
npm install -g scriptc
```

## Check dynamic-tier coverage first

Because `undici` does low-level socket I/O, it's worth confirming its whole
dependency graph is covered by scriptc's Node-builtin shims before building:

```bash
npm run coverage
```

This reports what compiles statically, what runs in the dynamic island, and
names any unshimmed builtin explicitly rather than failing silently.

## Build

```bash
npm run build
./bin/exchange-rate
```

Or compile-and-run in one step without producing a binary:

```bash
npm start
```

## Usage

```bash
./bin/exchange-rate                # 1 USD -> PHP (defaults)
./bin/exchange-rate USD PHP 100    # 100 USD -> PHP
./bin/exchange-rate EUR PHP 50     # 50 EUR -> PHP
```

Arguments are positional: `<base> <target> <amount>`, all optional, defaulting
to `USD PHP 1`.

## Data source

Rates come from the free, no-API-key **open access** endpoint of
ExchangeRate-API (`https://open.er-api.com/v6/latest/<BASE>`), updated once
daily. Attribution: [Rates By Exchange Rate API](https://www.exchangerate-api.com).
For higher-frequency updates or historical data you'd need a keyed plan or a
different provider (e.g. Frankfurter for ECB-based historical rates).

## Plain Node.js (no scriptc)

The source is ordinary TypeScript, so it also runs on plain Node for local
testing:

```bash
npx tsc src/index.ts --outDir dist --module commonjs --target ES2022 \
  --esModuleInterop --skipLibCheck --moduleResolution bundler
node dist/index.js
```

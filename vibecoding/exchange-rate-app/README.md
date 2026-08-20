# exchange-rate-app

Fetches live exchange rates and converts a base currency to a target
currency — defaults to **USD → PHP**. Compiled to a native binary with
**scriptc** (Vercel Labs' TypeScript-to-native compiler).

Uses scriptc's **built-in `fetch`**, which compiles fully statically — no
`--dynamic` flag, no embedded engine, no npm HTTP dependency at all.

## Error handling

Every fallible step returns an `AppResult<T>` — either `AppSuccess<T>` or
`AppError` — instead of throwing:

```ts
export interface AppSuccess<T> {
  readonly ok: true;
  readonly data: T;
}

export interface AppError {
  readonly ok: false;
  readonly message: string;
  readonly cause?: unknown;
}

export type AppResult<T> = AppSuccess<T> | AppError;
```

Callers narrow with `if (result.ok)` rather than try/catch. See
`src/types.ts` for the full type definitions (including the raw
`ExchangeRateApiResponse` shape and the validated `ExchangeRate` result)
and `src/index.ts` for how `getExchangeRate()` builds an `AppResult`.

## Setup

```bash
npm install
```

Requires Node.js 24+ and clang for scriptc itself:

```bash
npm install -g scriptc
```

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

The source is ordinary TypeScript, so it also runs on plain Node 18+ (which
has built-in `fetch`) for local testing:

```bash
npx tsc src/index.ts src/types.ts --outDir dist --module commonjs \
  --target ES2022 --esModuleInterop --skipLibCheck --moduleResolution bundler
node dist/index.js
```

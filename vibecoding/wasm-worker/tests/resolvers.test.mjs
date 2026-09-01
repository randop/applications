// tests/resolvers.test.mjs
// Exercises the real GraphQL schema, resolvers, TursoClient, and the compiled
// wasm engine together, with `fetch` mocked to stand in for Turso's HTTP API.
// This validates the whole request path except the actual network call.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { graphql } from "graphql";

import { schema, createRootValue } from "../src/graphql/schema.js";
import { TursoClient } from "../src/db/turso.js";
import { CurrencyEngine } from "../src/wasm/currency.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// ---- in-memory fake matching migrations/0001_init.sql -------------------

const currencies = [
  { code: "USD", name: "US Dollar", symbol: "$", decimal_places: 2 },
  { code: "EUR", name: "Euro", symbol: "\u20AC", decimal_places: 2 },
  { code: "GBP", name: "British Pound", symbol: "\u00A3", decimal_places: 2 },
  { code: "JPY", name: "Japanese Yen", symbol: "\u00A5", decimal_places: 0 },
];

let rates = [
  { base_code: "USD", quote_code: "EUR", rate: 0.9235, fetched_at: "2026-09-01T00:00:00.000Z" },
  { base_code: "USD", quote_code: "GBP", rate: 0.7912, fetched_at: "2026-09-01T00:00:00.000Z" },
  { base_code: "USD", quote_code: "JPY", rate: 149.82, fetched_at: "2026-09-01T00:00:00.000Z" },
];

function cell(v) {
  if (v === null || v === undefined) return { type: "null" };
  if (typeof v === "number" && Number.isInteger(v)) return { type: "integer", value: String(v) };
  if (typeof v === "number") return { type: "float", value: v };
  return { type: "text", value: String(v) };
}

function execResult(cols, rowObjs) {
  return {
    type: "ok",
    response: {
      type: "execute",
      result: {
        cols: cols.map((name) => ({ name })),
        rows: rowObjs.map((obj) => cols.map((c) => cell(obj[c]))),
        last_insert_rowid: null,
        affected_row_count: rowObjs.length,
      },
    },
  };
}

function argVal(a) {
  // request body args are already plain JS values encoded via toHranaValue;
  // decode enough to compare against our fake tables.
  if (a.type === "null") return null;
  if (a.type === "integer") return Number(a.value);
  if (a.type === "float") return a.value;
  return a.value;
}

globalThis.fetch = async (url, init) => {
  if (!String(url).endsWith("/v2/pipeline")) throw new Error(`unexpected fetch to ${url}`);
  const body = JSON.parse(init.body);
  const results = [];

  for (const req of body.requests) {
    if (req.type === "close") {
      results.push({ type: "ok", response: { type: "close" } });
      continue;
    }
    const sql = req.stmt.sql;
    const args = req.stmt.args.map(argVal);

    if (sql.includes("FROM currencies") && sql.includes("WHERE code = ?")) {
      const row = currencies.find((c) => c.code === args[0]);
      results.push(execResult(["code", "name", "symbol", "decimal_places"], row ? [row] : []));
    } else if (sql.startsWith("SELECT code, name, symbol, decimal_places FROM currencies")) {
      results.push(
        execResult(
          ["code", "name", "symbol", "decimal_places"],
          [...currencies].sort((a, b) => a.code.localeCompare(b.code))
        )
      );
    } else if (sql.includes("MAX(fetched_at)")) {
      // "latest rate per pair" -- our fake data already has one row per pair
      results.push(
        execResult(["base_code", "quote_code", "rate", "fetched_at"], rates)
      );
    } else if (sql.includes("ORDER BY fetched_at DESC LIMIT 1")) {
      const [base, quote] = args;
      const matches = rates
        .filter((r) => r.base_code === base && r.quote_code === quote)
        .sort((a, b) => (a.fetched_at < b.fetched_at ? 1 : -1));
      results.push(execResult(["base_code", "quote_code", "rate", "fetched_at"], matches.slice(0, 1)));
    } else if (sql.includes("ORDER BY fetched_at DESC LIMIT ?")) {
      const [base, quote, limit] = args;
      const matches = rates
        .filter((r) => r.base_code === base && r.quote_code === quote)
        .sort((a, b) => (a.fetched_at < b.fetched_at ? 1 : -1));
      results.push(execResult(["base_code", "quote_code", "rate", "fetched_at"], matches.slice(0, limit)));
    } else if (sql.startsWith("INSERT INTO currencies")) {
      const [code, name, symbol, decimal_places] = args;
      const existing = currencies.find((c) => c.code === code);
      if (existing) Object.assign(existing, { name, symbol, decimal_places });
      else currencies.push({ code, name, symbol, decimal_places });
      results.push(execResult([], []));
    } else if (sql.startsWith("INSERT INTO exchange_rates")) {
      const [base_code, quote_code, rate, fetched_at] = args;
      rates.push({ base_code, quote_code, rate, fetched_at });
      results.push(execResult([], []));
    } else {
      throw new Error(`mock fetch: unhandled SQL: ${sql}`);
    }
  }

  return {
    ok: true,
    json: async () => ({ results }),
    text: async () => JSON.stringify({ results }),
  };
};

// ---- run the actual GraphQL schema against the mock ----------------------

let failures = 0;
function check(label, condition) {
  if (condition) console.log(`ok   ${label}`);
  else {
    failures++;
    console.error(`FAIL ${label}`);
  }
}

const db = new TursoClient("https://fake.turso.io", "fake-token");
const engine = await CurrencyEngine.load(await readFile(path.join(__dirname, "..", "wasm", "currency.wasm")));
const rootValue = createRootValue();
const contextValue = { db, engine };

// 1. list currencies
{
  const res = await graphql({ schema, source: "{ currencies { code decimalPlaces } }", rootValue, contextValue });
  check("currencies query has no errors", !res.errors);
  check("currencies returns 4 rows", res.data.currencies.length === 4);
}

// 2. direct conversion
{
  const res = await graphql({
    schema,
    source: `{ convert(amount: 100, from: "USD", to: "EUR") { result method effectiveRate to { code } } }`,
    rootValue,
    contextValue,
  });
  check("convert USD->EUR has no errors", !res.errors);
  check("convert USD->EUR method is DIRECT", res.data?.convert.method === "DIRECT");
  check("convert USD->EUR result ~= 92.35", Math.abs(res.data?.convert.result - 92.35) < 0.005);
}

// 3. inverse conversion (EUR->USD, no direct row exists)
{
  const res = await graphql({
    schema,
    source: `{ convert(amount: 92.35, from: "EUR", to: "USD") { result method } }`,
    rootValue,
    contextValue,
  });
  check("convert EUR->USD has no errors", !res.errors);
  check("convert EUR->USD method is INVERSE", res.data?.convert.method === "INVERSE");
  check("convert EUR->USD result ~= 100", Math.abs(res.data?.convert.result - 100) < 0.05);
}

// 4. bridge conversion (EUR->JPY via USD, neither direct nor inverse exists)
{
  const res = await graphql({
    schema,
    source: `{ convert(amount: 100, from: "EUR", to: "JPY") { result method } }`,
    rootValue,
    contextValue,
  });
  check("convert EUR->JPY has no errors", !res.errors);
  check("convert EUR->JPY method is BRIDGE", res.data?.convert.method === "BRIDGE");
  // 100 EUR -> USD (100/0.9235=108.28) -> JPY (*149.82=16218) roughly
  check("convert EUR->JPY result in plausible range", res.data?.convert.result > 16000 && res.data?.convert.result < 16400);
}

// 5. convertToMany
{
  const res = await graphql({
    schema,
    source: `{ convertToMany(amount: 50, from: "USD", to: ["EUR", "GBP", "JPY"]) { to { code } result method } }`,
    rootValue,
    contextValue,
  });
  check("convertToMany has no errors", !res.errors);
  check("convertToMany returns 3 results", res.data?.convertToMany.length === 3);
  check("convertToMany all DIRECT (all have USD rows)", res.data?.convertToMany.every((r) => r.method === "DIRECT"));
}

// 6. mutation: setRate then read it back via rate query
{
  const m = await graphql({
    schema,
    source: `mutation { setRate(base: "USD", quote: "CHF", rate: 0.88) { rate base { code } quote { code } } }`,
    rootValue,
    contextValue,
  });
  check("setRate mutation errors on unknown currency CHF (expected, not seeded)", !!m.errors);
}

// 7. addCurrency then setRate then rate query round-trip
{
  const add = await graphql({
    schema,
    source: `mutation { addCurrency(code: "CHF", name: "Swiss Franc", symbol: "Fr", decimalPlaces: 2) { code } }`,
    rootValue,
    contextValue,
  });
  check("addCurrency CHF succeeds", !add.errors && add.data.addCurrency.code === "CHF");

  const set = await graphql({
    schema,
    source: `mutation { setRate(base: "USD", quote: "CHF", rate: 0.88) { rate } }`,
    rootValue,
    contextValue,
  });
  check("setRate USD->CHF succeeds", !set.errors && set.data.setRate.rate === 0.88);

  const read = await graphql({
    schema,
    source: `{ rate(base: "USD", quote: "CHF") { rate base { code } quote { code } } }`,
    rootValue,
    contextValue,
  });
  check("rate query reads back USD->CHF", !read.errors && read.data.rate.rate === 0.88);
}

// 8. unknown currency code produces a GraphQL error, not a crash
{
  const res = await graphql({
    schema,
    source: `{ convert(amount: 10, from: "USD", to: "ZZZ") { result } }`,
    rootValue,
    contextValue,
  });
  check("unknown currency code yields a GraphQL error", res.errors && res.errors.length > 0);
}

console.log(failures === 0 ? "\nAll resolver tests passed." : `\n${failures} test(s) FAILED.`);
process.exit(failures === 0 ? 0 : 1);

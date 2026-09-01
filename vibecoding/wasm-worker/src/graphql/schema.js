// src/graphql/schema.js
//
// Schema + resolvers for the currency conversion API, built on graphql-js
// (the reference GraphQL execution engine). All monetary arithmetic is
// delegated to the CurrencyEngine (WASM) using integer minor-units; this
// layer only does I/O (Turso) and shaping data for GraphQL, never math on
// floats.

import { GraphQLSchema } from "graphql";
import { buildSchema } from "graphql/utilities/index.js";

export const typeDefs = /* GraphQL */ `
  """An ISO-4217-style currency known to this service."""
  type Currency {
    "3-letter currency code, e.g. USD"
    code: String!
    name: String!
    symbol: String
    "Number of decimal digits used for display/rounding, e.g. 2 for USD, 0 for JPY"
    decimalPlaces: Int!
  }

  """A stored exchange rate: 1 base = rate quote, as of fetchedAt."""
  type ExchangeRate {
    base: Currency!
    quote: Currency!
    rate: Float!
    fetchedAt: String!
  }

  """How a conversion's rate was resolved."""
  enum ConversionMethod {
    DIRECT
    INVERSE
    BRIDGE
  }

  type ConversionResult {
    amount: Float!
    from: Currency!
    to: Currency!
    "Effective rate actually applied (post inversion/bridging)"
    effectiveRate: Float!
    result: Float!
    method: ConversionMethod!
    "Timestamp of the newest rate row used to compute this result"
    asOf: String!
  }

  type Query {
    currencies: [Currency!]!
    currency(code: String!): Currency

    "Most recent stored rate for a base/quote pair, if any (no bridging)."
    rate(base: String!, quote: String!): ExchangeRate

    "Recent rate history for a base/quote pair, newest first."
    rateHistory(base: String!, quote: String!, limit: Int = 20): [ExchangeRate!]!

    "Convert amount from one currency into another, bridging via USD if needed."
    convert(amount: Float!, from: String!, to: String!): ConversionResult!

    "Convert amount from one currency into several targets in a single call."
    convertToMany(amount: Float!, from: String!, to: [String!]!): [ConversionResult!]!
  }

  type Mutation {
    addCurrency(code: String!, name: String!, symbol: String, decimalPlaces: Int = 2): Currency!

    "Records a new observed rate for base->quote (append-only; history is preserved)."
    setRate(base: String!, quote: String!, rate: Float!): ExchangeRate!
  }
`;

export const schema = buildSchema(typeDefs);

// ---------------------------------------------------------------------------
// Helpers shared by resolvers
// ---------------------------------------------------------------------------

function assertCurrencyCode(code) {
  if (!/^[A-Za-z]{3}$/.test(code)) {
    throw new Error(`Invalid currency code "${code}": expected a 3-letter ISO-4217-style code`);
  }
  return code.toUpperCase();
}

/** Loads every currency row once per request and indexes it for O(1) lookups. */
async function loadCurrencyIndex(db) {
  const rows = await db.query(
    "SELECT code, name, symbol, decimal_places FROM currencies ORDER BY code"
  );
  const byCode = new Map();
  rows.forEach((row, i) => byCode.set(row.code, { ...toCurrency(row), idx: i }));
  return { rows, byCode };
}

function toCurrency(row) {
  return { code: row.code, name: row.name, symbol: row.symbol, decimalPlaces: row.decimal_places };
}

function requireCurrency(index, code) {
  const c = index.byCode.get(code);
  if (!c) throw new Error(`Unknown currency code "${code}"`);
  return c;
}

/** Loads the latest rate per (base,quote) pair present in the DB, for bridging/lookup. */
async function loadLatestRates(db) {
  return db.query(`
    SELECT base_code, quote_code, rate, fetched_at
    FROM exchange_rates er
    WHERE fetched_at = (
      SELECT MAX(fetched_at) FROM exchange_rates
      WHERE base_code = er.base_code AND quote_code = er.quote_code
    )
  `);
}

// The wasm engine always operates on one fixed internal fixed-point scale
// (1e8), completely independent of any currency's display decimal places.
// Mixing per-currency decimal places into the *arithmetic* (rather than only
// using them for final display rounding) is a classic FX bug: a rate like
// "1 USD = 149.82 JPY" relates whole units, so scaling the input by USD's
// decimal places (2) and the output by JPY's (0) before/after a single
// multiply silently introduces a 100x error whenever the two currencies'
// decimal places differ. Converting to/from a currency-agnostic scale first
// avoids that entirely; decimalPlaces is applied once, purely for display.
const INTERNAL_SCALE = 100_000_000; // must match RATE_SCALE in currency.cpp

function toInternalScale(amount) {
  return BigInt(Math.round(amount * INTERNAL_SCALE));
}

function fromInternalScale(amountScaled, displayDecimalPlaces) {
  const raw = Number(amountScaled) / INTERNAL_SCALE;
  const factor = 10 ** displayDecimalPlaces;
  return Math.round(raw * factor) / factor;
}

// ---------------------------------------------------------------------------
// Resolver factory. `ctx` provides { db: TursoClient, engine: CurrencyEngine }.
// ---------------------------------------------------------------------------

export function createRootValue() {
  return {
    async currencies(_args, ctx) {
      const rows = await ctx.db.query(
        "SELECT code, name, symbol, decimal_places FROM currencies ORDER BY code"
      );
      return rows.map(toCurrency);
    },

    async currency({ code }, ctx) {
      const row = await ctx.db.query(
        "SELECT code, name, symbol, decimal_places FROM currencies WHERE code = ?",
        [assertCurrencyCode(code)]
      );
      return row[0] ? toCurrency(row[0]) : null;
    },

    async rate({ base, quote }, ctx) {
      base = assertCurrencyCode(base);
      quote = assertCurrencyCode(quote);
      const rows = await ctx.db.query(
        `SELECT base_code, quote_code, rate, fetched_at FROM exchange_rates
         WHERE base_code = ? AND quote_code = ?
         ORDER BY fetched_at DESC LIMIT 1`,
        [base, quote]
      );
      if (!rows[0]) return null;
      const index = await loadCurrencyIndex(ctx.db);
      return {
        base: requireCurrency(index, rows[0].base_code),
        quote: requireCurrency(index, rows[0].quote_code),
        rate: rows[0].rate,
        fetchedAt: rows[0].fetched_at,
      };
    },

    async rateHistory({ base, quote, limit }, ctx) {
      base = assertCurrencyCode(base);
      quote = assertCurrencyCode(quote);
      const rows = await ctx.db.query(
        `SELECT base_code, quote_code, rate, fetched_at FROM exchange_rates
         WHERE base_code = ? AND quote_code = ?
         ORDER BY fetched_at DESC LIMIT ?`,
        [base, quote, limit ?? 20]
      );
      const index = await loadCurrencyIndex(ctx.db);
      return rows.map((r) => ({
        base: requireCurrency(index, r.base_code),
        quote: requireCurrency(index, r.quote_code),
        rate: r.rate,
        fetchedAt: r.fetched_at,
      }));
    },

    async convert({ amount, from, to }, ctx) {
      from = assertCurrencyCode(from);
      to = assertCurrencyCode(to);
      if (!(amount >= 0)) throw new Error("amount must be >= 0");

      const [index, rateRows] = await Promise.all([loadCurrencyIndex(ctx.db), loadLatestRates(ctx.db)]);
      const fromC = requireCurrency(index, from);
      const toC = requireCurrency(index, to);
      const bridge = requireCurrency(index, "USD");

      ctx.engine.reset();
      const rateTable = ctx.engine.writeRates(
        rateRows.map((r) => ({
          baseIdx: requireCurrency(index, r.base_code).idx,
          quoteIdx: requireCurrency(index, r.quote_code).idx,
          rate: r.rate,
        }))
      );

      const amountScaled = toInternalScale(amount);
      const { amount: resultScaled, kind } = ctx.engine.convert(
        amountScaled,
        fromC.idx,
        toC.idx,
        bridge.idx,
        rateTable
      );
      if (kind === "NOT_FOUND" || kind === "BAD_INPUT") {
        throw new Error(`No exchange rate path found from ${from} to ${to}`);
      }

      const newestFetchedAt = rateRows.reduce(
        (max, r) => (r.fetched_at > max ? r.fetched_at : max),
        ""
      );
      const resultAmount = fromInternalScale(resultScaled, toC.decimalPlaces);
      return {
        amount,
        from: fromC,
        to: toC,
        effectiveRate: amount > 0 ? resultAmount / amount : 0,
        result: resultAmount,
        method: kind,
        asOf: newestFetchedAt || null,
      };
    },

    async convertToMany({ amount, from, to }, ctx) {
      from = assertCurrencyCode(from);
      const targets = to.map(assertCurrencyCode);
      if (!(amount >= 0)) throw new Error("amount must be >= 0");

      const [index, rateRows] = await Promise.all([loadCurrencyIndex(ctx.db), loadLatestRates(ctx.db)]);
      const fromC = requireCurrency(index, from);
      const bridge = requireCurrency(index, "USD");
      const targetCs = targets.map((code) => requireCurrency(index, code));

      ctx.engine.reset();
      const rateTable = ctx.engine.writeRates(
        rateRows.map((r) => ({
          baseIdx: requireCurrency(index, r.base_code).idx,
          quoteIdx: requireCurrency(index, r.quote_code).idx,
          rate: r.rate,
        }))
      );

      const amountScaled = toInternalScale(amount);
      const results = ctx.engine.convertToMany(
        amountScaled,
        fromC.idx,
        bridge.idx,
        rateTable,
        targetCs.map((c) => c.idx)
      );

      const newestFetchedAt = rateRows.reduce(
        (max, r) => (r.fetched_at > max ? r.fetched_at : max),
        ""
      );

      return results.map(({ amount: resultScaled, kind }, i) => {
        if (kind === "NOT_FOUND" || kind === "BAD_INPUT") {
          throw new Error(`No exchange rate path found from ${from} to ${targets[i]}`);
        }
        const resultAmount = fromInternalScale(resultScaled, targetCs[i].decimalPlaces);
        return {
          amount,
          from: fromC,
          to: targetCs[i],
          effectiveRate: amount > 0 ? resultAmount / amount : 0,
          result: resultAmount,
          method: kind,
          asOf: newestFetchedAt || null,
        };
      });
    },

    async addCurrency({ code, name, symbol, decimalPlaces }, ctx) {
      code = assertCurrencyCode(code);
      await ctx.db.execute(
        `INSERT INTO currencies (code, name, symbol, decimal_places) VALUES (?, ?, ?, ?)
         ON CONFLICT(code) DO UPDATE SET name = excluded.name, symbol = excluded.symbol,
           decimal_places = excluded.decimal_places`,
        [code, name, symbol ?? null, decimalPlaces ?? 2]
      );
      return { code, name, symbol: symbol ?? null, decimalPlaces: decimalPlaces ?? 2 };
    },

    async setRate({ base, quote, rate }, ctx) {
      base = assertCurrencyCode(base);
      quote = assertCurrencyCode(quote);
      if (!(rate > 0)) throw new Error("rate must be > 0");

      const index = await loadCurrencyIndex(ctx.db);
      const baseC = requireCurrency(index, base);
      const quoteC = requireCurrency(index, quote);

      const fetchedAt = new Date().toISOString();
      await ctx.db.execute(
        `INSERT INTO exchange_rates (base_code, quote_code, rate, fetched_at) VALUES (?, ?, ?, ?)`,
        [base, quote, rate, fetchedAt]
      );
      return { base: baseC, quote: quoteC, rate, fetchedAt };
    },
  };
}

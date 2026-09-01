-- migrations/0001_init.sql
--
-- Apply with the Turso CLI, e.g.:
--   turso db shell <your-db-name> < migrations/0001_init.sql
-- or via the HTTP API using the same TursoClient this service uses.

CREATE TABLE IF NOT EXISTS currencies (
  code           TEXT PRIMARY KEY CHECK (length(code) = 3),
  name           TEXT NOT NULL,
  symbol         TEXT,
  decimal_places INTEGER NOT NULL DEFAULT 2 CHECK (decimal_places BETWEEN 0 AND 6)
);

CREATE TABLE IF NOT EXISTS exchange_rates (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  base_code  TEXT NOT NULL REFERENCES currencies(code),
  quote_code TEXT NOT NULL REFERENCES currencies(code),
  rate       REAL NOT NULL CHECK (rate > 0),
  fetched_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
  UNIQUE (base_code, quote_code, fetched_at)
);

CREATE INDEX IF NOT EXISTS idx_rates_pair_time
  ON exchange_rates (base_code, quote_code, fetched_at DESC);

-- Seed a handful of currencies. USD is treated as the bridge currency by the
-- conversion engine, so it should always be present with rates to/from it.
INSERT INTO currencies (code, name, symbol, decimal_places) VALUES
  ('USD', 'US Dollar',        '$', 2),
  ('EUR', 'Euro',             '€', 2),
  ('GBP', 'British Pound',    '£', 2),
  ('JPY', 'Japanese Yen',     '¥', 0),
  ('CAD', 'Canadian Dollar',  '$', 2),
  ('AUD', 'Australian Dollar','$', 2),
  ('CHF', 'Swiss Franc',      'Fr',2),
  ('CNY', 'Chinese Yuan',     '¥', 2)
ON CONFLICT (code) DO NOTHING;

-- Seed illustrative rates, all quoted against USD as base (edit/replace with
-- real data from your FX provider; setRate mutations append new rows and
-- keep full history rather than overwriting).
INSERT INTO exchange_rates (base_code, quote_code, rate, fetched_at) VALUES
  ('USD', 'EUR', 0.9235,  '2026-09-01T00:00:00.000Z'),
  ('USD', 'GBP', 0.7912,  '2026-09-01T00:00:00.000Z'),
  ('USD', 'JPY', 149.82,  '2026-09-01T00:00:00.000Z'),
  ('USD', 'CAD', 1.3721,  '2026-09-01T00:00:00.000Z'),
  ('USD', 'AUD', 1.5203,  '2026-09-01T00:00:00.000Z'),
  ('USD', 'CHF', 0.8801,  '2026-09-01T00:00:00.000Z'),
  ('USD', 'CNY', 7.1064,  '2026-09-01T00:00:00.000Z');

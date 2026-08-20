/**
 * Fetches live exchange rates and converts a base currency to a target
 * currency (defaults to USD -> PHP).
 *
 * Uses scriptc's built-in `fetch` — its static runtime ships its own
 * from-scratch fetch implementation (scr_fetch.c), so this compiles fully
 * statically with no --dynamic flag and no embedded engine needed.
 *
 * Every fallible step returns an AppResult<T> (AppSuccess<T> | AppError)
 * instead of throwing, so callers narrow with `if (result.ok)` rather than
 * try/catch.
 */
import { ok, err } from "./types";
import type { AppResult, ExchangeRate, ExchangeRateApiResponse } from "./types";

const API_BASE = "https://open.er-api.com/v6/latest";
const DEFAULT_BASE_CURRENCY = "USD";
const DEFAULT_TARGET_CURRENCY = "PHP";

async function getExchangeRate(
  baseCurrency: string,
  targetCurrency: string,
  amount: number,
): Promise<AppResult<ExchangeRate>> {
  const url = `${API_BASE}/${baseCurrency}`;

  let response: Response;
  try {
    response = await fetch(url, {
      method: "GET",
      headers: { accept: "application/json" },
    });
  } catch (cause) {
    return err(`Network request to ${url} failed`, cause);
  }

  if (!response.ok) {
    return err(`Exchange rate request failed with HTTP ${response.status}`);
  }

  let data: ExchangeRateApiResponse;
  try {
    data = (await response.json()) as ExchangeRateApiResponse;
  } catch (cause) {
    return err("Could not parse exchange rate response as JSON", cause);
  }

  if (data.result !== "success") {
    return err(`Exchange rate API returned result="${data.result}"`);
  }

  const rate = data.rates[targetCurrency];
  if (rate === undefined) {
    return err(`No rate found for currency code "${targetCurrency}"`);
  }

  return ok({
    base: baseCurrency,
    target: targetCurrency,
    rate,
    amount,
    converted: amount * rate,
    lastUpdated: data.time_last_update_utc,
  });
}

// scriptc compiles arrays as dense buffers: an out-of-bounds index is a
// runtime trap, not `undefined`, so the length is checked first.
function argOrDefault(index: number, fallback: string): string {
  return process.argv.length > index ? process.argv[index] : fallback;
}

async function main(): Promise<void> {
  const baseCurrency = argOrDefault(2, DEFAULT_BASE_CURRENCY).toUpperCase();
  const targetCurrency = argOrDefault(3, DEFAULT_TARGET_CURRENCY).toUpperCase();
  const amountArg = argOrDefault(4, "1");
  const amount = Number(amountArg);

  if (!Number.isFinite(amount)) {
    console.error(`Invalid amount: "${amountArg}"`);
    process.exit(1);
    return;
  }

  console.log(`Fetching ${baseCurrency} -> ${targetCurrency} exchange rate...`);

  const result = await getExchangeRate(baseCurrency, targetCurrency, amount);

  if (!result.ok) {
    console.error("Error:", result.message);
    if (result.cause !== undefined) {
      console.error("Cause:", result.cause);
    }
    process.exit(1);
    return;
  }

  const { rate, converted } = result.data;
  console.log(`1 ${baseCurrency} = ${rate} ${targetCurrency}`);
  console.log(`${amount} ${baseCurrency} = ${converted.toFixed(2)} ${targetCurrency}`);
}

main();

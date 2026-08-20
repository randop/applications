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

// Grouped thousands separators, e.g. 1000 -> "1,000". Used for the input
// amount, where the original precision (whole number or a few decimals)
// should be preserved as typed. Plain toLocaleString("en-US") with no
// options bag — scriptc's static tier only lowers that default-options
// form (SC2020 for the options-bag overload, see formatFixed below).
function formatAmount(value: number): string {
  return value.toLocaleString("en-US");
}

// Inserts thousands separators into a run of ASCII digits, e.g.
// "58423" -> "58,423".
function withThousandsSeparators(digits: string): string {
  let result = "";
  let sinceGroup = 0;
  for (let i = digits.length - 1; i >= 0; i--) {
    result = digits[i] + result;
    sinceGroup++;
    if (sinceGroup === 3 && i !== 0) {
      result = "," + result;
      sinceGroup = 0;
    }
  }
  return result;
}

function isAllZeroDigits(value: string): boolean {
  for (let i = 0; i < value.length; i++) {
    const ch = value[i];
    if (ch !== "0" && ch !== ".") {
      return false;
    }
  }
  return true;
}

/**
 * Grouped thousands separators with an exact fixed number of decimal
 * places, e.g. formatFixed(58423.1, 2) -> "58,423.10". Built from
 * Number.prototype.toFixed (statically supported) plus manual grouping,
 * because scriptc's static tier only lowers the no-options-bag form of
 * toLocaleString — minimumFractionDigits/maximumFractionDigits hit
 * SC2020 ("has no scriptc lowering yet").
 */
function formatFixed(value: number, fractionDigits: number): string {
  const isNegative = value < 0;
  const fixed = Math.abs(value).toFixed(fractionDigits);
  const dotIndex = fixed.indexOf(".");
  const integerDigits = dotIndex === -1 ? fixed : fixed.slice(0, dotIndex);
  const fractionPart = dotIndex === -1 ? "" : fixed.slice(dotIndex);
  const grouped = withThousandsSeparators(integerDigits);
  const sign = isNegative && !isAllZeroDigits(fixed) ? "-" : "";
  return `${sign}${grouped}${fractionPart}`;
}

// Money-style: grouped, pinned to exactly 2 decimals, e.g. 58500 -> "58,500.00".
function formatMoney(value: number): string {
  return formatFixed(value, 2);
}

// Grouped, with enough fraction digits to keep small rates (e.g. PHP -> USD
// at ~0.0177) readable instead of rounding them away.
function formatRate(value: number): string {
  return formatFixed(value, 6);
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
  console.log(`1 ${baseCurrency} = ${formatRate(rate)} ${targetCurrency}`);
  console.log(
    `${formatAmount(amount)} ${baseCurrency} = ${formatMoney(converted)} ${targetCurrency}`,
  );
}

main();

/**
 * Same behavior as src/index.ts, but uses scriptc's built-in `fetch`
 * instead of the `undici` package. scriptc's static runtime ships its own
 * from-scratch fetch implementation (scr_fetch.c) deliberately built to
 * match undici's wire behavior (header order, redirect handling, error
 * shapes) byte-for-byte — so this compiles fully statically, with no
 * --dynamic flag and no embedded engine at all.
 *
 * Use this if `--dynamic` + `undici` hits a compiler-immaturity bug (worth
 * filing at https://github.com/vercel-labs/scriptc/issues) and you just
 * need the exchange-rate fetch working right now.
 */

interface ExchangeRateResponse {
  result: string;
  base_code: string;
  time_last_update_utc: string;
  rates: Record<string, number>;
}

const API_BASE = "https://open.er-api.com/v6/latest";
const DEFAULT_BASE_CURRENCY = "USD";
const DEFAULT_TARGET_CURRENCY = "PHP";

async function getExchangeRate(
  baseCurrency: string,
  targetCurrency: string,
): Promise<number> {
  const url = `${API_BASE}/${baseCurrency}`;

  const response = await fetch(url, {
    method: "GET",
    headers: { accept: "application/json" },
  });

  if (!response.ok) {
    throw new Error(`Exchange rate request failed with HTTP ${response.status}`);
  }

  const data = (await response.json()) as ExchangeRateResponse;

  if (data.result !== "success") {
    throw new Error(`Exchange rate API returned result="${data.result}"`);
  }

  const rate = data.rates[targetCurrency];
  if (rate === undefined) {
    throw new Error(`No rate found for currency code "${targetCurrency}"`);
  }

  return rate;
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

  const rate = await getExchangeRate(baseCurrency, targetCurrency);
  const converted = amount * rate;

  console.log(`1 ${baseCurrency} = ${rate} ${targetCurrency}`);
  console.log(`${amount} ${baseCurrency} = ${converted.toFixed(2)} ${targetCurrency}`);
}

main().catch((err: unknown) => {
  console.error("Error:", err instanceof Error ? err.message : String(err));
  process.exit(1);
});

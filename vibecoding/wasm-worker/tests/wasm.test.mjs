// tests/wasm.test.mjs
// Sanity-checks the compiled currency.wasm module directly with Node's
// WebAssembly engine (the same engine V8/Cloudflare Workers uses), independent
// of the rest of the Worker. Run: node tests/wasm.test.mjs
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const wasmPath = path.join(__dirname, "..", "wasm", "currency.wasm");

const RATE_SCALE = 100000000n; // must match RATE_SCALE in currency.cpp

function scaleRate(rate) {
  // rate as a JS number, e.g. 0.9235 -> fixed-point bigint
  return BigInt(Math.round(rate * 1e8));
}

const CONV_DIRECT = 0, CONV_INVERSE = 1, CONV_BRIDGE = 2, CONV_NOT_FOUND = -1, CONV_BAD_INPUT = -2;
const kindName = { [-2]: "BAD_INPUT", [-1]: "NOT_FOUND", 0: "DIRECT", 1: "INVERSE", 2: "BRIDGE" };

let failures = 0;
function assertEqual(actual, expected, label) {
  if (actual !== expected) {
    failures++;
    console.error(`FAIL ${label}: expected ${expected}, got ${actual}`);
  } else {
    console.log(`ok   ${label}: ${actual}`);
  }
}

const bytes = await readFile(wasmPath);
const { instance } = await WebAssembly.instantiate(bytes, {});
const { memory, alloc, reset_arena, convert, batch_convert } = instance.exports;

// currency index table for this test: 0=USD 1=EUR 2=JPY 3=GBP
const USD = 0, EUR = 1, JPY = 2, GBP = 3;

// Rate table as it would come from Turso: USD->EUR and USD->JPY are direct
// (bridge currency is USD), GBP has no rate to anything -> should NOT_FOUND
// except via bridge once we add USD->GBP too.
const rates = [
  { base: USD, quote: EUR, rate: 0.9235 },
  { base: USD, quote: JPY, rate: 149.82 },
  { base: EUR, quote: GBP, rate: 0.8567 }, // direct EUR->GBP, no USD->GBP at all
];

reset_arena();
const ratesPtr = alloc(rates.length * 16); // {i32 base, i32 quote, i64 rate_scaled}
const view = new DataView(memory.buffer);
rates.forEach((r, i) => {
  const off = ratesPtr + i * 16;
  view.setInt32(off + 0, r.base, true);
  view.setInt32(off + 4, r.quote, true);
  view.setBigUint64(off + 8, scaleRate(r.rate), true);
});

function doConvert(amountMinorUnits, from, to) {
  const statusPtr = alloc(4);
  const amountScaled = BigInt(amountMinorUnits); // already in "minor units" fixed-point
  const result = convert(amountScaled, from, to, USD, ratesPtr, rates.length, statusPtr);
  const status = new DataView(memory.buffer).getInt32(statusPtr, true);
  return { result, status };
}

// $100.00 (10000 minor units, i.e. cents) USD -> EUR, direct rate 0.9235
{
  const { result, status } = doConvert(10000, USD, EUR);
  assertEqual(kindName[status], "DIRECT", "USD->EUR uses direct rate");
  assertEqual(result, 9235n, "$100.00 USD -> EUR (expect 92.35 EUR = 9235 cents)");
}

// same-currency identity
{
  const { result, status } = doConvert(12345, JPY, JPY);
  assertEqual(kindName[status], "DIRECT", "same-currency short-circuits as DIRECT");
  assertEqual(result, 12345n, "identity conversion returns input unchanged");
}

// EUR -> USD should use the INVERSE of USD->EUR (0.9235)
{
  const { result, status } = doConvert(9235, EUR, USD);
  assertEqual(kindName[status], "INVERSE", "EUR->USD uses inverse of USD->EUR");
  // 9235 cents EUR / 0.9235 ~= 10000 cents USD (allow 1-cent rounding)
  const diff = result > 10000n ? result - 10000n : 10000n - result;
  if (diff > 1n) { failures++; console.error(`FAIL EUR->USD inverse: got ${result}, expected ~10000`); }
  else console.log(`ok   EUR->USD inverse ~= 10000 (got ${result})`);
}

// JPY -> EUR has no direct/inverse pair, must bridge through USD
{
  const { result, status } = doConvert(1000000, JPY, EUR); // 10,000.00 JPY (2dp minor units)
  assertEqual(kindName[status], "BRIDGE", "JPY->EUR must bridge through USD");
  // 10000 JPY / 149.82 * 0.9235 ~= 61.65 EUR -> 6165 (minor units), allow small rounding
  console.log(`     JPY->EUR bridged result (minor units): ${result}`);
  if (result < 6100n || result > 6250n) { failures++; console.error(`FAIL JPY->EUR bridge out of expected range: ${result}`); }
}

// GBP has only EUR->GBP direct and no USD leg at all -> from GBP to JPY must be NOT_FOUND
{
  const { result, status } = doConvert(500, GBP, JPY);
  assertEqual(kindName[status], "NOT_FOUND", "GBP->JPY has no path (no USD leg for GBP)");
  assertEqual(result, 0n, "NOT_FOUND returns 0");
}

// negative amount is rejected
{
  const { result, status } = doConvert(-100, USD, EUR);
  assertEqual(kindName[status], "BAD_INPUT", "negative amount rejected");
  assertEqual(result, 0n, "BAD_INPUT returns 0");
}

// batch_convert: USD 100.00 -> [EUR, JPY]
{
  const targetsPtr = alloc(2 * 4);
  const resultsPtr = alloc(2 * 8);
  const statusesPtr = alloc(2 * 4);
  const dv = new DataView(memory.buffer);
  dv.setInt32(targetsPtr + 0, EUR, true);
  dv.setInt32(targetsPtr + 4, JPY, true);
  batch_convert(10000n, USD, USD, ratesPtr, rates.length, targetsPtr, 2, resultsPtr, statusesPtr);
  const eurOut = dv.getBigInt64(resultsPtr + 0, true);
  const jpyOut = dv.getBigInt64(resultsPtr + 8, true);
  assertEqual(eurOut, 9235n, "batch_convert USD->EUR matches single convert");
  assertEqual(jpyOut, 1498200n, "batch_convert USD->JPY (100.00 * 149.82 = 14982.00 -> 1498200 minor units)");
}

console.log(failures === 0 ? "\nAll wasm tests passed." : `\n${failures} test(s) FAILED.`);
process.exit(failures === 0 ? 0 : 1);

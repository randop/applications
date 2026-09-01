// src/wasm/currency.js
//
// Thin JS wrapper around wasm/currency.wasm. Owns the memory-marshalling
// details (writing the rate table into the module's arena, reading results
// back out) so the GraphQL resolvers never touch raw pointers directly.

const RATE_SCALE = 100_000_000n; // must match RATE_SCALE in currency.cpp

const CONV_KIND = Object.freeze({
  0: "DIRECT",
  1: "INVERSE",
  2: "BRIDGE",
  "-1": "NOT_FOUND",
  "-2": "BAD_INPUT",
});

export class CurrencyEngine {
  /** @param {WebAssembly.Instance} instance */
  constructor(instance) {
    this.exports = instance.exports;
  }

  /**
   * @param {WebAssembly.Module | ArrayBuffer | Uint8Array} wasmModule
   *   In a Cloudflare Worker, import the .wasm file as an ES module
   *   (`import wasmModule from "../../wasm/currency.wasm"`) and pass the
   *   resulting WebAssembly.Module straight through here.
   */
  static async load(wasmModule) {
    const instance =
      wasmModule instanceof WebAssembly.Module
        ? await WebAssembly.instantiate(wasmModule, {})
        : (await WebAssembly.instantiate(wasmModule, {})).instance;
    return new CurrencyEngine(instance);
  }

  /** Converts a decimal rate (e.g. 0.9235) into the module's fixed-point representation. */
  static scaleRate(rate) {
    return BigInt(Math.round(rate * 1e8));
  }

  /**
   * Writes a rate table into the module's arena and returns a handle used by
   * `convert`/`convertToMany` within the same request. Call `reset()` first.
   *
   * @param {{baseIdx: number, quoteIdx: number, rate: number}[]} rates
   */
  writeRates(rates) {
    const { alloc, memory } = this.exports;
    const ptr = alloc(rates.length * 16);
    if (ptr === 0 && rates.length > 0) {
      throw new Error("wasm arena exhausted while writing rate table");
    }
    const view = new DataView(memory.buffer);
    rates.forEach((r, i) => {
      const off = ptr + i * 16;
      view.setInt32(off + 0, r.baseIdx, true);
      view.setInt32(off + 4, r.quoteIdx, true);
      view.setBigUint64(off + 8, CurrencyEngine.scaleRate(r.rate), true);
    });
    return { ptr, count: rates.length };
  }

  /** Must be called once at the start of handling each GraphQL request. */
  reset() {
    this.exports.reset_arena();
  }

  /**
   * @param {bigint|number} amountScaled amount already scaled by 1e8 (the
   *   engine's fixed internal precision) as an integer -- never a float, and
   *   never scaled by a currency's own display decimal places (see the
   *   comment above INTERNAL_SCALE in src/graphql/schema.js for why mixing
   *   those in is a correctness bug, not just a style choice).
   * @param {number} fromIdx currency index
   * @param {number} toIdx currency index
   * @param {number} bridgeIdx currency index used as the bridge (typically USD)
   * @param {{ptr: number, count: number}} rateTable from writeRates()
   * @returns {{amount: bigint, kind: "DIRECT"|"INVERSE"|"BRIDGE"|"NOT_FOUND"|"BAD_INPUT"}}
   */
  convert(amountScaled, fromIdx, toIdx, bridgeIdx, rateTable) {
    const { alloc, memory, convert } = this.exports;
    const statusPtr = alloc(4);
    const amount = convert(
      BigInt(amountScaled),
      fromIdx,
      toIdx,
      bridgeIdx,
      rateTable.ptr,
      rateTable.count,
      statusPtr
    );
    const status = new DataView(memory.buffer).getInt32(statusPtr, true);
    return { amount, kind: CONV_KIND[String(status)] ?? "NOT_FOUND" };
  }

  /**
   * Converts one amount into several target currencies in a single wasm call.
   * @param {number[]} targetIdxs
   */
  convertToMany(amountScaled, fromIdx, bridgeIdx, rateTable, targetIdxs) {
    const { alloc, memory, batch_convert } = this.exports;
    const targetsPtr = alloc(targetIdxs.length * 4);
    const resultsPtr = alloc(targetIdxs.length * 8);
    const statusesPtr = alloc(targetIdxs.length * 4);

    const dv = new DataView(memory.buffer);
    targetIdxs.forEach((idx, i) => dv.setInt32(targetsPtr + i * 4, idx, true));

    batch_convert(
      BigInt(amountScaled),
      fromIdx,
      bridgeIdx,
      rateTable.ptr,
      rateTable.count,
      targetsPtr,
      targetIdxs.length,
      resultsPtr,
      statusesPtr
    );

    // Re-read the DataView: memory.buffer may have been detached/regrown by
    // an intervening allocation.
    const out = new DataView(memory.buffer);
    return targetIdxs.map((_, i) => ({
      amount: out.getBigInt64(resultsPtr + i * 8, true),
      kind: CONV_KIND[String(out.getInt32(statusesPtr + i * 4, true))] ?? "NOT_FOUND",
    }));
  }
}

export { RATE_SCALE };

// currency.cpp
//
// Freestanding C++ -> WebAssembly module for Cloudflare Workers.
// No libc, no libc++, no exceptions, no RTTI. Compiled with clang++ targeting
// wasm32-unknown-unknown (see build.sh) and instantiated by the JS Worker in
// src/wasm/currency.js.
//
// Why this exists as WASM rather than plain JS:
//   - Money math must never touch IEEE-754 floats (0.1 + 0.2 problems). Every
//     amount and rate is carried as a scaled 64-bit integer, and the only
//     multiply/divide operation the engine needs (amount * rate / SCALE) is
//     implemented with a manual 128-bit-precision integer routine, so results
//     are exact and deterministic across platforms.
//   - The rate graph resolution (direct / inverse / bridge-via-USD) is a hot,
//     tight numeric loop that benefits from running as compiled WASM instead
//     of interpreted JS, and is trivially portable to other hosts later.
//
// Memory protocol with the JS host:
//   - The module owns a static arena inside its own linear memory. JS calls
//     `alloc(n)` to get a writable offset, writes bytes via a Uint8Array view
//     over the exported `memory`, then calls `convert(...)` / `batch_convert`
//     passing that offset. JS calls `reset_arena()` once per request.

using i8  = signed char;
using u8  = unsigned char;
using i32 = signed int;
using u32 = unsigned int;
using i64 = signed long long;
using u64 = unsigned long long;

extern "C" {

// ---------------------------------------------------------------------------
// Bump allocator over a static arena living in the module's own memory.
// ---------------------------------------------------------------------------

static constexpr u32 ARENA_BYTES = 4u * 1024u * 1024u; // 4 MiB
static u8 g_arena[ARENA_BYTES];
static u32 g_offset = 0;

__attribute__((visibility("default")))
u32 alloc(u32 size) {
    u32 aligned = (g_offset + 7u) & ~7u;
    if ((u64)aligned + (u64)size > (u64)ARENA_BYTES) {
        return 0; // signals out-of-arena to the caller
    }
    g_offset = aligned + size;
    return (u32)(u64)(g_arena + aligned);
}

__attribute__((visibility("default")))
void reset_arena() {
    g_offset = 0;
}

__attribute__((visibility("default")))
u32 arena_capacity() {
    return ARENA_BYTES;
}

// ---------------------------------------------------------------------------
// Exact unsigned 64x64 -> 128 multiply, then 128 / 64 -> 64 divide.
// No __int128, no compiler-rt libcalls: pure 64-bit ops only, safe to link
// with -nostdlib. Caller guarantees the true quotient fits in 64 bits, which
// holds for any realistic currency amount/rate combination.
// ---------------------------------------------------------------------------

static u64 mul_div_u64(u64 a, u64 b, u64 d) {
    if (d == 0) return 0;

    u64 a_lo = a & 0xFFFFFFFFull, a_hi = a >> 32;
    u64 b_lo = b & 0xFFFFFFFFull, b_hi = b >> 32;

    u64 t0 = a_lo * b_lo;
    u64 t1 = a_lo * b_hi;
    u64 t2 = a_hi * b_lo;
    u64 t3 = a_hi * b_hi;

    u64 mid  = (t0 >> 32) + (t1 & 0xFFFFFFFFull) + (t2 & 0xFFFFFFFFull);
    u64 hi   = t3 + (t1 >> 32) + (t2 >> 32) + (mid >> 32);
    u64 lo   = (mid << 32) | (t0 & 0xFFFFFFFFull);

    // 128-bit (hi:lo) / 64-bit d, via restoring binary long division.
    u64 quotient = 0;
    u64 remainder = 0;
    for (i32 i = 127; i >= 0; i--) {
        u64 bit = (i >= 64) ? ((hi >> (i - 64)) & 1ull) : ((lo >> i) & 1ull);
        remainder = (remainder << 1) | bit;
        quotient <<= 1;
        if (remainder >= d) {
            remainder -= d;
            quotient |= 1ull;
        }
    }
    return quotient;
}

// ---------------------------------------------------------------------------
// Rate graph resolution + conversion.
// ---------------------------------------------------------------------------

// Fixed-point scale applied to every rate (8 decimal digits of precision,
// matching what the Turso `exchange_rates.rate` column stores as text/real).
static constexpr u64 RATE_SCALE = 100000000ull; // 1e8

struct RateEntry {
    i32 base;         // currency index
    i32 quote;        // currency index
    u64 rate_scaled;  // (quote per 1 base) * RATE_SCALE
};

enum ConvKind : i32 {
    CONV_DIRECT    = 0,
    CONV_INVERSE   = 1,
    CONV_BRIDGE    = 2,
    CONV_NOT_FOUND = -1,
    CONV_BAD_INPUT = -2,
};

static bool find_rate(const RateEntry* rates, i32 count, i32 from, i32 to,
                       u64* out_rate_scaled, i32* out_kind) {
    for (i32 i = 0; i < count; i++) {
        if (rates[i].base == from && rates[i].quote == to) {
            *out_rate_scaled = rates[i].rate_scaled;
            *out_kind = CONV_DIRECT;
            return true;
        }
    }
    for (i32 i = 0; i < count; i++) {
        if (rates[i].base == to && rates[i].quote == from && rates[i].rate_scaled != 0) {
            // invert: 1 / rate, kept at the same fixed-point scale
            *out_rate_scaled = mul_div_u64(RATE_SCALE, RATE_SCALE, rates[i].rate_scaled);
            *out_kind = CONV_INVERSE;
            return true;
        }
    }
    return false;
}

// Resolve amount_scaled (in minor units, e.g. cents) of `from` into `to`,
// trying a direct rate, then its inverse, then a bridge through bridge_idx
// (normally the USD row). rates_ptr points at rate_count RateEntry structs
// previously written into the arena by the host.
//
// Writes the resolution kind into *out_status (a ConvKind) and returns the
// converted amount (0 if not found / bad input).
__attribute__((visibility("default")))
i64 convert(i64 amount_scaled, i32 from_idx, i32 to_idx, i32 bridge_idx,
            u32 rates_ptr, i32 rate_count, u32 out_status_ptr) {
    i32* out_status = (i32*)(u64)out_status_ptr;

    if (amount_scaled < 0) {
        *out_status = CONV_BAD_INPUT;
        return 0;
    }
    if (from_idx == to_idx) {
        *out_status = CONV_DIRECT;
        return amount_scaled;
    }

    const RateEntry* rates = (const RateEntry*)(u64)rates_ptr;
    u64 rate_scaled;
    i32 kind;

    if (find_rate(rates, rate_count, from_idx, to_idx, &rate_scaled, &kind)) {
        *out_status = kind;
        return (i64)mul_div_u64((u64)amount_scaled, rate_scaled, RATE_SCALE);
    }

    if (from_idx != bridge_idx && to_idx != bridge_idx) {
        u64 r1, r2;
        i32 k1, k2;
        if (find_rate(rates, rate_count, from_idx, bridge_idx, &r1, &k1) &&
            find_rate(rates, rate_count, bridge_idx, to_idx, &r2, &k2)) {
            *out_status = CONV_BRIDGE;
            u64 mid = mul_div_u64((u64)amount_scaled, r1, RATE_SCALE);
            return (i64)mul_div_u64(mid, r2, RATE_SCALE);
        }
    }

    *out_status = CONV_NOT_FOUND;
    return 0;
}

// Convenience: convert the same amount against every entry in a target list
// in one call, so the JS resolver doesn't have to cross the wasm/js boundary
// per-currency for the `convertToMany` GraphQL field.
//
// targets_ptr: i32[target_count] of destination currency indices.
// results_ptr: i64[target_count] output amounts.
// statuses_ptr: i32[target_count] output ConvKind per target.
__attribute__((visibility("default")))
void batch_convert(i64 amount_scaled, i32 from_idx, i32 bridge_idx,
                    u32 rates_ptr, i32 rate_count,
                    u32 targets_ptr, i32 target_count,
                    u32 results_ptr, u32 statuses_ptr) {
    const i32* targets = (const i32*)(u64)targets_ptr;
    i64* results = (i64*)(u64)results_ptr;
    i32* statuses = (i32*)(u64)statuses_ptr;

    for (i32 t = 0; t < target_count; t++) {
        i32 status;
        i64 amount = convert(amount_scaled, from_idx, targets[t], bridge_idx,
                              rates_ptr, rate_count,
                              (u32)(u64)&status);
        results[t] = amount;
        statuses[t] = status;
    }
}

} // extern "C"

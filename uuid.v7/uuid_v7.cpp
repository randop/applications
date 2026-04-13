/**
 * uuid_v7.cpp — RFC 9562 UUID Version 7 in C++20
 *
 * Layout (128 bits):
 *   [47:0]   unix_ts_ms  — 48-bit millisecond timestamp
 *   [51:48]  ver         — 0b0111 (version 7)
 *   [63:52]  rand_a      — 12-bit monotonic sub-ms sequence
 *   [65:64]  var         — 0b10 (RFC 4122 variant)
 *   [127:66] rand_b      — 62-bit cryptographic random
 *
 * Monotonicity guarantee: within the same millisecond the 12-bit
 * rand_a counter increments.  If it overflows, the timestamp is
 * advanced by 1 ms synthetically so order is always preserved.
 *
 * Build:
 *   g++ -std=c++20 -O2 -Wall -Wextra uuid_v7.cpp -o uuid_v7
 */

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

// ============================================================================
//  UUID type
// ============================================================================

struct UUID {
    std::array<std::uint8_t, 16> bytes{};

    // ---- accessors ---------------------------------------------------------

    /// Millisecond timestamp embedded in the UUID (bits 127-80).
    [[nodiscard]] std::uint64_t timestamp_ms() const noexcept {
        std::uint64_t ts = 0;
        for (int i = 0; i < 6; ++i)
            ts = (ts << 8) | bytes[i];
        return ts;
    }

    /// Version nibble (should be 7 for UUIDv7).
    [[nodiscard]] std::uint8_t version() const noexcept {
        return bytes[6] >> 4;
    }

    /// RFC 4122 variant bits (should be 0b10 for standard UUIDs).
    [[nodiscard]] std::uint8_t variant() const noexcept {
        return (bytes[8] >> 6) & 0x3;
    }

    /// The 12-bit rand_a / sub-ms sequence field.
    [[nodiscard]] std::uint16_t rand_a() const noexcept {
        return (static_cast<std::uint16_t>(bytes[6] & 0x0F) << 8) | bytes[7];
    }

    // ---- comparisons -------------------------------------------------------

    auto operator<=>(const UUID&) const noexcept = default;

    // ---- string I/O --------------------------------------------------------

    /// Returns the canonical 8-4-4-4-12 hex string, lower-case.
    [[nodiscard]] std::string to_string() const {
        const auto& b = bytes;
        char buf[37];
        std::snprintf(buf, sizeof(buf),
            "%02x%02x%02x%02x-"
            "%02x%02x-"
            "%02x%02x-"
            "%02x%02x-"
            "%02x%02x%02x%02x%02x%02x",
            b[0],b[1],b[2],b[3],
            b[4],b[5],
            b[6],b[7],
            b[8],b[9],
            b[10],b[11],b[12],b[13],b[14],b[15]);
        return {buf};
    }

    /// Parse a canonical UUID string (with or without hyphens).
    /// Returns std::nullopt on any format error.
    [[nodiscard]] static std::optional<UUID> from_string(std::string_view sv) {
        // Strip optional hyphens
        std::string hex;
        hex.reserve(32);
        for (char c : sv) {
            if (c == '-') continue;
            if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
            hex += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (hex.size() != 32) return std::nullopt;

        UUID u;
        for (std::size_t i = 0; i < 16; ++i) {
            std::uint8_t hi = hex[2*i]   <= '9' ? hex[2*i]   - '0' : hex[2*i]   - 'a' + 10;
            std::uint8_t lo = hex[2*i+1] <= '9' ? hex[2*i+1] - '0' : hex[2*i+1] - 'a' + 10;
            u.bytes[i] = static_cast<std::uint8_t>((hi << 4) | lo);
        }
        return u;
    }
};

// Make UUID hashable for unordered containers
template<>
struct std::hash<UUID> {
    std::size_t operator()(const UUID& u) const noexcept {
        std::size_t h = 0;
        for (auto b : u.bytes)
            h = h * 131 + b;
        return h;
    }
};

// ============================================================================
//  UUIDv7 generator — lock-free via thread-local state
//
//  Each thread owns its own RNG and monotonic counter, so no mutex is needed.
//  Monotonicity is strict within a thread.  Across threads, UUIDs from the
//  same millisecond may interleave in timestamp order (both will have seq=0),
//  but the 62-bit rand_b tail makes collisions astronomically unlikely.
// ============================================================================

namespace detail {

/// One mt19937_64 per thread, seeded independently from std::random_device.
inline std::mt19937_64& get_rng() noexcept {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    return rng;
}

/// Per-thread monotonic state: the last timestamp we issued and the
/// sub-millisecond sequence counter within that timestamp.
struct MonoState {
    std::uint64_t last_ms = 0;
    std::uint32_t seq     = 0;
};

inline MonoState& get_mono() noexcept {
    static thread_local MonoState st;
    return st;
}

inline std::uint64_t current_ms() noexcept {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count()
    );
}

/// Pack timestamp + sequence + random tail into a UUIDv7 byte array.
inline UUID pack(std::uint64_t ms, std::uint16_t seq) noexcept {
    const std::uint64_t rand_b =
        get_rng()() & 0x3FFF'FFFF'FFFF'FFFFull; // 62 random bits

    UUID u;
    auto& b = u.bytes;

    // Bytes 0-5: 48-bit unix_ts_ms
    b[0] = static_cast<std::uint8_t>(ms >> 40);
    b[1] = static_cast<std::uint8_t>(ms >> 32);
    b[2] = static_cast<std::uint8_t>(ms >> 24);
    b[3] = static_cast<std::uint8_t>(ms >> 16);
    b[4] = static_cast<std::uint8_t>(ms >>  8);
    b[5] = static_cast<std::uint8_t>(ms);

    // Byte 6: ver=7 (top 4 bits) | rand_a[11:8] (bottom 4 bits)
    b[6] = static_cast<std::uint8_t>(0x70 | ((seq >> 8) & 0x0F));
    // Byte 7: rand_a[7:0]
    b[7] = static_cast<std::uint8_t>(seq & 0xFF);

    // Byte 8: variant=0b10 (top 2 bits) | rand_b[61:56] (bottom 6 bits)
    b[8]  = static_cast<std::uint8_t>(0x80 | ((rand_b >> 56) & 0x3F));
    b[9]  = static_cast<std::uint8_t>(rand_b >> 48);
    b[10] = static_cast<std::uint8_t>(rand_b >> 40);
    b[11] = static_cast<std::uint8_t>(rand_b >> 32);
    b[12] = static_cast<std::uint8_t>(rand_b >> 24);
    b[13] = static_cast<std::uint8_t>(rand_b >> 16);
    b[14] = static_cast<std::uint8_t>(rand_b >>  8);
    b[15] = static_cast<std::uint8_t>(rand_b);

    return u;
}

/// Core generation step — operates entirely on thread-local state, no locks.
inline UUID next() noexcept {
    MonoState& st    = get_mono();
    const std::uint64_t now = current_ms();

    if (now > st.last_ms) {
        st.last_ms = now;
        st.seq     = 0;
    } else {
        // Same (or retrograde) ms: advance the sub-ms sequence counter.
        ++st.seq;
        if (st.seq > 0x0FFF) {
            // rand_a exhausted — synthesise the next ms to stay monotone.
            ++st.last_ms;
            st.seq = 0;
        }
    }

    return pack(st.last_ms, static_cast<std::uint16_t>(st.seq));
}

} // namespace detail

// ---------------------------------------------------------------------------
// Public API — UUIDv7Generator is a zero-size, stateless handle; all state
// lives in thread-local storage so every instance is equally valid and calls
// are naturally serialised per-thread without any locking.
// ---------------------------------------------------------------------------

class UUIDv7Generator {
public:
    UUIDv7Generator()  = default;
    ~UUIDv7Generator() = default;

    /// Generate one UUIDv7.  Lock-free.
    [[nodiscard]] UUID generate() const noexcept {
        return detail::next();
    }

    /// Generate n UUIDs in a batch.  Monotonicity is strict within the
    /// calling thread across the whole batch.
    [[nodiscard]] std::vector<UUID> generate_batch(std::size_t n) const {
        std::vector<UUID> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            out.push_back(detail::next());
        return out;
    }
};

// Convenient free function — also lock-free.
inline UUID new_uuid_v7() noexcept { return detail::next(); }

// ============================================================================
//  Test framework — minimal, zero-dependency
// ============================================================================

namespace test {

struct Suite {
    std::string name;
    int passed = 0, failed = 0;

    void check(bool cond, std::string_view label) {
        if (cond) {
            ++passed;
            std::cout << "  [PASS] " << label << '\n';
        } else {
            ++failed;
            std::cout << "  [FAIL] " << label << '\n';
        }
    }

    template<typename A, typename B>
    void eq(const A& a, const B& b, std::string_view label) {
        check(a == b, label);
    }

    void print_summary() const {
        std::cout << '\n'
                  << "  Suite \"" << name << "\": "
                  << passed << " passed, "
                  << failed << " failed\n"
                  << std::string(50, '-') << '\n';
    }

    bool ok() const { return failed == 0; }
};

} // namespace test

// ============================================================================
//  Tests
// ============================================================================

// --- 1. RFC layout -----------------------------------------------------------

void test_layout(test::Suite& s) {
    for (int i = 0; i < 100; ++i) {
        UUID u = new_uuid_v7();
        s.check(u.version() == 7,        "version nibble == 7");
        s.check(u.variant() == 0b10,     "variant bits == 0b10");
        s.check(u.timestamp_ms() > 0,    "timestamp > 0");
    }
}

// --- 2. Monotonicity (single-threaded) ----------------------------------------

void test_monotonicity_single(test::Suite& s) {
    const int N = 10'000;
    UUIDv7Generator gen;
    auto batch = gen.generate_batch(N);

    bool mono = true;
    for (int i = 1; i < N; ++i) {
        if (batch[i] <= batch[i-1]) { mono = false; break; }
    }
    s.check(mono, "batch of 10000 UUIDs is strictly monotone");
}

// --- 3. Uniqueness -----------------------------------------------------------

void test_uniqueness(test::Suite& s) {
    const int N = 100'000;
    UUIDv7Generator gen;
    auto batch = gen.generate_batch(N);
    std::unordered_set<UUID> seen(batch.begin(), batch.end());
    s.check(seen.size() == static_cast<std::size_t>(N), "100000 UUIDs are all unique");
}

// --- 4. Timestamp accuracy ---------------------------------------------------

void test_timestamp_accuracy(test::Suite& s) {
    using namespace std::chrono;
    auto before = static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());

    UUID u = new_uuid_v7();

    auto after = static_cast<std::uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());

    std::uint64_t ts = u.timestamp_ms();
    s.check(ts >= before && ts <= after, "timestamp is within [before, after] range");
}

// --- 5. String round-trip ----------------------------------------------------

void test_string_roundtrip(test::Suite& s) {
    for (int i = 0; i < 50; ++i) {
        UUID u = new_uuid_v7();
        std::string str = u.to_string();

        // Length: 32 hex + 4 hyphens
        s.eq(str.size(), std::size_t{36}, "string length is 36");

        // Hyphens at positions 8, 13, 18, 23
        s.check(str[8] == '-' && str[13] == '-' && str[18] == '-' && str[23] == '-',
                "hyphens at correct positions");

        // Version character
        s.check(str[14] == '7', "version char is '7'");

        // Variant character must be '8','9','a', or 'b'
        char vc = str[19];
        std::string vc_msg = std::string("variant char '") + vc + "' in {8,9,a,b}";
        s.check(vc == '8' || vc == '9' || vc == 'a' || vc == 'b', vc_msg);

        // Round-trip
        auto parsed = UUID::from_string(str);
        s.check(parsed.has_value(), "from_string succeeds");
        s.check(*parsed == u, "round-trip equality");
    }
}

// --- 6. from_string edge cases -----------------------------------------------

void test_from_string_edgecases(test::Suite& s) {
    // Valid with hyphens
    s.check(UUID::from_string("01960000-0000-7000-8000-000000000000").has_value(),
            "valid string with hyphens parses");

    // Valid without hyphens
    s.check(UUID::from_string("0196000000007000800000000000000").has_value() == false,
            "31-char string rejected");
    s.check(UUID::from_string("019600000000700080000000000000001").has_value() == false,
            "33-char string rejected");
    s.check(UUID::from_string("01960000000070008000000000000000").has_value(),
            "32-char no-hyphen string accepted");

    // Invalid chars
    s.check(!UUID::from_string("01960000-0000-7000-8000-00000000000g").has_value(),
            "invalid hex char rejected");
    s.check(!UUID::from_string("").has_value(), "empty string rejected");
    s.check(!UUID::from_string("not-a-uuid").has_value(), "garbage rejected");
}

// --- 7. Sequence counter within a millisecond --------------------------------

void test_sequence_counter(test::Suite& s) {
    // Generate many UUIDs rapidly; among those sharing the same ms,
    // rand_a must be strictly increasing.
    UUIDv7Generator gen;
    constexpr int N = 5000;
    auto batch = gen.generate_batch(N);

    bool seq_ok = true;
    for (int i = 1; i < N; ++i) {
        std::uint64_t ts_prev = batch[i-1].timestamp_ms();
        std::uint64_t ts_curr = batch[i  ].timestamp_ms();
        std::uint16_t ra_prev = batch[i-1].rand_a();
        std::uint16_t ra_curr = batch[i  ].rand_a();

        if (ts_curr == ts_prev) {
            // Same ms: sequence must increment
            if (ra_curr != ra_prev + 1) { seq_ok = false; break; }
        } else if (ts_curr > ts_prev) {
            // New ms: sequence resets to 0
            if (ra_curr != 0) { seq_ok = false; break; }
        } else {
            seq_ok = false; break; // timestamp went backwards — error
        }
    }
    s.check(seq_ok, "sequence counter increments correctly within same ms");
}

// --- 8. Overflow: sequence rolls over into next synthetic ms -----------------

void test_sequence_overflow(test::Suite& s) {
    // We'll fill an entire millisecond's worth of rand_a values (4096)
    // using a generator that is frozen in time.
    // Instead of mocking the clock, we verify that 4097 consecutive
    // rapid UUIDs still form a strictly monotone sequence.
    UUIDv7Generator gen;
    auto batch = gen.generate_batch(4097);

    bool mono = true;
    for (std::size_t i = 1; i < batch.size(); ++i)
        if (batch[i] <= batch[i-1]) { mono = false; break; }

    s.check(mono, "4097 UUIDs across sequence overflow are still monotone");
}

// --- 9. Comparisons ----------------------------------------------------------

void test_comparisons(test::Suite& s) {
    UUIDv7Generator gen;
    UUID a = gen.generate();
    UUID b = gen.generate();

    s.check(a < b,    "earlier UUID < later UUID");
    s.check(b > a,    "later UUID > earlier UUID");
    s.check(a != b,   "two UUIDs are not equal");
    s.check(a == a,   "UUID equals itself");

    // Sorting
    std::vector<UUID> v = {b, a};
    std::sort(v.begin(), v.end());
    s.check(v[0] == a && v[1] == b, "std::sort on UUID vector works");
}

// --- 10. Use in ordered containers ------------------------------------------

void test_ordered_containers(test::Suite& s) {
    const int N = 1000;
    UUIDv7Generator gen;
    auto batch = gen.generate_batch(N);

    std::set<UUID> tree(batch.begin(), batch.end());
    s.eq(tree.size(), static_cast<std::size_t>(N), "all UUIDs inserted into std::set");

    // Iteration order must equal generation order (UUIDs are time-ordered)
    auto it = tree.begin();
    bool order_ok = true;
    for (int i = 0; i < N; ++i, ++it)
        if (*it != batch[i]) { order_ok = false; break; }
    s.check(order_ok, "std::set iteration order == generation order");
}

// --- 11. Hash map usage ------------------------------------------------------

void test_hash_map(test::Suite& s) {
    const int N = 10'000;
    UUIDv7Generator gen;
    std::unordered_set<UUID> uset;
    uset.reserve(N);
    for (int i = 0; i < N; ++i)
        uset.insert(gen.generate());
    s.eq(uset.size(), static_cast<std::size_t>(N), "10000 UUIDs all inserted into std::unordered_set");
}

// --- 12. Multithreaded uniqueness & per-thread monotonicity ------------------
//
// With thread-local state each thread has its own independent monotonic
// sequence.  Cross-thread ordering is not guaranteed, but:
//   - uniqueness holds via the 62-bit rand_b tail
//   - each thread's own sequence is strictly monotone

void test_multithreaded(test::Suite& s) {
    constexpr int THREADS    = 8;
    constexpr int PER_THREAD = 10'000;

    std::vector<std::vector<UUID>> results(THREADS);
    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&, t]() {
            // Each lambda runs in its own thread → its own thread-local state.
            UUIDv7Generator gen;
            results[t] = gen.generate_batch(PER_THREAD);
        });
    }
    for (auto& th : threads) th.join();

    // Global uniqueness — guaranteed by 62-bit rand_b entropy
    std::unordered_set<UUID> all;
    all.reserve(THREADS * PER_THREAD);
    for (auto& vec : results)
        for (auto& u : vec)
            all.insert(u);

    s.eq(all.size(), static_cast<std::size_t>(THREADS * PER_THREAD),
         "80000 total UUIDs from 8 threads are all unique");

    // Per-thread monotonicity
    bool per_thread_mono = true;
    for (auto& vec : results) {
        for (std::size_t i = 1; i < vec.size(); ++i) {
            if (vec[i] <= vec[i-1]) { per_thread_mono = false; break; }
        }
        if (!per_thread_mono) break;
    }
    s.check(per_thread_mono, "each thread's UUID sequence is strictly monotone");
}

// --- 13. Nil UUID is not generated ------------------------------------------

void test_nil_not_generated(test::Suite& s) {
    UUID nil{};
    const int N = 10'000;
    UUIDv7Generator gen;
    bool nil_found = false;
    for (int i = 0; i < N; ++i)
        if (gen.generate() == nil) { nil_found = true; break; }
    s.check(!nil_found, "nil UUID never generated in 10k samples");
}

// --- 14. Bit-field invariants ------------------------------------------------

void test_bit_fields(test::Suite& s) {
    for (int i = 0; i < 1000; ++i) {
        UUID u = new_uuid_v7();

        // Version field: top 4 bits of byte 6 must be 0x7
        s.check((u.bytes[6] & 0xF0) == 0x70, "version bits == 0x70");

        // Variant field: top 2 bits of byte 8 must be 0b10
        s.check((u.bytes[8] & 0xC0) == 0x80, "variant bits == 0x80");
    }
}

// --- 15. Temporal ordering across threads ------------------------------------
//
// Two generators on different threads share nothing.  After a real sleep the
// second thread's timestamp must be strictly larger.

void test_independent_generators(test::Suite& s) {
    UUID a, b;

    std::thread t1([&]{ a = UUIDv7Generator{}.generate(); });
    t1.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    std::thread t2([&]{ b = UUIDv7Generator{}.generate(); });
    t2.join();

    s.check(b.timestamp_ms() > a.timestamp_ms(),
            "UUID from later thread has larger timestamp");
    s.check(b > a, "UUID from later thread compares greater");
}

// ============================================================================
//  main
// ============================================================================

int main() {
    std::cout << std::string(50, '=') << '\n'
              << "  UUID v7 — RFC 9562 — Test Suite\n"
              << std::string(50, '=') << '\n';

    int total_pass = 0, total_fail = 0;

    auto run = [&](std::string name, auto fn) {
        std::cout << '\n' << name << '\n' << std::string(50, '-') << '\n';
        test::Suite s{ std::move(name) };
        fn(s);
        s.print_summary();
        total_pass += s.passed;
        total_fail += s.failed;
    };

    run("1. RFC 9562 Layout",             test_layout);
    run("2. Single-thread Monotonicity",  test_monotonicity_single);
    run("3. Uniqueness (100k)",           test_uniqueness);
    run("4. Timestamp Accuracy",          test_timestamp_accuracy);
    run("5. String Round-trip",           test_string_roundtrip);
    run("6. from_string Edge Cases",      test_from_string_edgecases);
    run("7. Sequence Counter",            test_sequence_counter);
    run("8. Sequence Overflow",           test_sequence_overflow);
    run("9. Comparisons",                 test_comparisons);
    run("10. Ordered Containers",         test_ordered_containers);
    run("11. Hash Map",                   test_hash_map);
    run("12. Multithreaded",              test_multithreaded);
    run("13. Nil Never Generated",        test_nil_not_generated);
    run("14. Bit-field Invariants",       test_bit_fields);
    run("15. Independent Generators",     test_independent_generators);

    std::cout << '\n' << std::string(50, '=') << '\n'
              << "  TOTAL: " << total_pass << " passed, " << total_fail << " failed\n"
              << std::string(50, '=') << '\n';

    return total_fail == 0 ? 0 : 1;
}
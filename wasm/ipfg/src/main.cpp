#include <cstdint>
#include <string_view>
#include <array>
#include <optional>
#include <iostream>

extern "C" {
    extern const std::uint8_t _binary_ip_ranges_bin_start[];
    extern const std::uint8_t _binary_ip_ranges_bin_end[];

    extern const std::uint8_t _binary_ip_strings_bin_start[];
    extern const std::uint8_t _binary_ip_strings_bin_end[];
}

constexpr std::uint32_t read_le32(const std::uint8_t* p) noexcept {
    return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
}

std::optional<std::uint32_t> ip_to_uint(std::string_view s) noexcept {
    std::array<std::uint8_t,4> o{};
    std::size_t i = 0;
    std::uint32_t v = 0;
    for (char c : s) {
        if (c == '.') {
            if (i >= 4 || v > 255) return {};
            o[i++] = static_cast<std::uint8_t>(v);
            v = 0;
            continue;
        }
        if (c < '0' || c > '9') return {};
        v = v * 10 + (c - '0');
        if (v > 255) return {};
    }
    if (i != 3) return {};
    o[3] = static_cast<std::uint8_t>(v);
    return (o[0]<<24) | (o[1]<<16) | (o[2]<<8) | o[3];
}

// Main lookup by numeric IP
std::pair<std::string_view, std::string_view> geoip_lookup(std::uint32_t ip) noexcept {
    const auto* rstart = _binary_ip_ranges_bin_start;
    const auto* rend   = _binary_ip_ranges_bin_end;
    std::size_t rsize  = rend - rstart;

    const auto* sstart = _binary_ip_strings_bin_start;
    const auto* send   = _binary_ip_strings_bin_end;
    std::size_t ssize  = send - sstart;

    std::size_t count = rsize / 16;
    if (count == 0) return {"ZZ", "Unknown"};

    std::size_t low = 0, high = count;
    while (low < high) {
        std::size_t mid = low + (high - low) / 2;
        const std::uint8_t* rec = rstart + mid * 16;
        if (read_le32(rec) <= ip) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    if (low == 0) return {"ZZ", "Unknown"};

    const std::uint8_t* rec = rstart + (low - 1) * 16;
    std::uint32_t start = read_le32(rec);
    std::uint32_t end   = read_le32(rec + 4);

    if (ip < start || ip > end) return {"ZZ", "Unknown"};

    std::uint32_t coff = read_le32(rec + 8);
    std::uint32_t toff = read_le32(rec + 12);

    auto get_str = [&](std::uint32_t off) -> std::string_view {
        if (off >= ssize) return {};
        const char* p = reinterpret_cast<const char*>(sstart + off);
        const char* e = p;
        while (e < reinterpret_cast<const char*>(send) && *e) ++e;
        return {p, static_cast<std::size_t>(e - p)};
    };

    return {get_str(coff), get_str(toff)};
}

// Overload for string like "192.168.1.1"
std::pair<std::string_view, std::string_view> geoip_lookup(std::string_view ip_str) noexcept {
    if (auto ip = ip_to_uint(ip_str)) {
        return geoip_lookup(*ip);
    }
    return {"ZZ", "Unknown"};
}

std::string read_line_stdin()
{
    std::string line;
    line.reserve(64);
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n' && ch != '\r') {
        line.push_back(static_cast<char>(ch));
    }
    // discard possible trailing '\r' or '\n'
    if (ch == '\r') {
        int nxt = getchar();
        if (nxt != '\n') ungetc(nxt, stdin);
    }
    return line;
}

extern "C" {
  // WASI‑compatible entry point
  void _start() {
    std::string line = read_line_stdin();
    if (line.empty()) {
      std::printf("void\n");
      std::fflush(stdout);
      return;
    }
    int parts[4];
    if (std::sscanf(line.c_str(), "%d.%d.%d.%d",
                    &parts[0], &parts[1], &parts[2], &parts[3]) == 4) {
      auto [country, city] = geoip_lookup(line);
      std::printf("%s,%.*s,%.*s\n", line.c_str(), static_cast<int>(country.size()), country.data(), static_cast<int>(city.size()), city.data());
      std::fflush(stdout);
    }
    std::printf("%s,%s,%s\n", "Hello", "World", "WASM");
    std::fflush(stdout);
  }
}

/*
int main() {
    auto [country, city] = geoip_lookup("8.8.8.8");
    std::cout << "8.8.8.8     → Country: " << country << ", City: " << city << '\n';

    auto [c2, city2] = geoip_lookup("1.1.1.1");
    std::cout << "1.1.1.1     → Country: " << c2 << ", City: " << city2 << '\n';

    auto [c3, city3] = geoip_lookup("192.168.1.1");
    std::cout << "192.168.1.1 → Country: " << c3 << ", City: " << city3 << '\n';

    auto [c4, city4] = geoip_lookup("175.158.203.133");
    std::cout << "175.158.203.133 → Country: " << c4 << ", City: " << city4 << '\n';

    return 0;
}
*/

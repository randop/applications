#include "sysmon.hpp"

#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;

namespace sysmon {
namespace {

// Fast integer parse from string_view (no exceptions, no allocation).
template <typename T>
[[nodiscard]] bool parse_uint(std::string_view sv, T &out) noexcept {
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
  return ec == std::errc{} && ptr == sv.data() + sv.size();
}

[[nodiscard]] bool parse_int(std::string_view sv, std::int32_t &out) noexcept {
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
  return ec == std::errc{} && ptr == sv.data() + sv.size();
}

// Split a line of space-separated tokens without allocating a vector of
// strings. Returns number of tokens written into out[].
template <std::size_t N>
[[nodiscard]] std::size_t
tokenize(std::string_view line, std::array<std::string_view, N> &out) noexcept {
  std::size_t count = 0;
  std::size_t start = 0;
  const std::size_t len = line.size();
  while (start < len && count < N) {
    while (start < len && (line[start] == ' ' || line[start] == '\t')) {
      ++start;
    }
    if (start >= len) {
      break;
    }
    std::size_t end = start;
    while (end < len && line[end] != ' ' && line[end] != '\t') {
      ++end;
    }
    out[count++] = line.substr(start, end - start);
    start = end;
  }
  return count;
}

[[nodiscard]] bool parse_cpu_line(std::string_view line,
                                  CpuTimes &times) noexcept {
  // Expected: "cpu" or "cpuN" followed by up to 10 counters.
  std::array<std::string_view, 12> tok{};
  const auto n = tokenize(line, tok);
  if (n < 5) {
    return false; // at least user..idle
  }

  // Skip the label (tok[0]).
  const auto fields = n - 1;
  std::array<std::uint64_t, 10> vals{};
  for (std::size_t i = 0; i < fields && i < 10; ++i) {
    if (!parse_uint(tok[i + 1], vals[i])) {
      return false;
    }
  }

  times.user = vals[0];
  times.nice = vals[1];
  times.system = vals[2];
  times.idle = vals[3];
  times.iowait = fields > 4 ? vals[4] : 0;
  times.irq = fields > 5 ? vals[5] : 0;
  times.softirq = fields > 6 ? vals[6] : 0;
  times.steal = fields > 7 ? vals[7] : 0;
  times.guest = fields > 8 ? vals[8] : 0;
  times.guest_nice = fields > 9 ? vals[9] : 0;
  return true;
}

[[nodiscard]] double utilization(const CpuTimes &a,
                                 const CpuTimes &b) noexcept {
  const auto total_delta = b.total() - a.total();
  if (total_delta == 0) {
    return 0.0;
  }
  const auto busy_delta = b.busy() - a.busy();
  // Guard against rare counter wrap / race (should not happen on 64-bit).
  if (busy_delta > total_delta) {
    return 100.0;
  }
  return 100.0 * static_cast<double>(busy_delta) /
         static_cast<double>(total_delta);
}

} // namespace

Result<CpuSnapshot> Monitor::read_cpu(bool include_cores) noexcept {
  std::ifstream ifs("/proc/stat");
  if (!ifs.is_open()) {
    return std::unexpected(Error::IoFailure);
  }

  CpuSnapshot snap;
  line_buf_.clear();

  // First line is always the aggregate "cpu ".
  if (!std::getline(ifs, line_buf_)) {
    return std::unexpected(Error::ParseFailure);
  }
  if (!parse_cpu_line(line_buf_, snap.aggregate)) {
    return std::unexpected(Error::ParseFailure);
  }

  if (include_cores) {
    snap.cores.reserve(64); // typical upper bound; grows only once
    while (std::getline(ifs, line_buf_)) {
      if (line_buf_.size() < 4 || line_buf_[0] != 'c' || line_buf_[1] != 'p' ||
          line_buf_[2] != 'u') {
        break; // past the per-cpu lines
      }
      // Skip the aggregate if it somehow appears again; accept "cpu0", "cpu1",
      // ...
      if (line_buf_[3] == ' ' || line_buf_[3] == '\t') {
        continue;
      }

      CpuTimes core{};
      if (parse_cpu_line(line_buf_, core)) {
        snap.cores.push_back(core);
      }
    }
  }

  return snap;
}

Result<double> Monitor::cpu_usage_percent(const CpuSnapshot &prev,
                                          const CpuSnapshot &curr) noexcept {
  return utilization(prev.aggregate, curr.aggregate);
}

Result<std::vector<double>>
Monitor::cpu_core_usage_percent(const CpuSnapshot &prev,
                                const CpuSnapshot &curr) noexcept {
  if (prev.cores.size() != curr.cores.size()) {
    return std::unexpected(Error::InvalidArgument);
  }
  std::vector<double> out;
  out.reserve(prev.cores.size());
  for (std::size_t i = 0; i < prev.cores.size(); ++i) {
    out.push_back(utilization(prev.cores[i], curr.cores[i]));
  }
  return out;
}

Result<MemoryInfo> Monitor::read_memory() noexcept {
  std::ifstream ifs("/proc/meminfo");
  if (!ifs.is_open()) {
    return std::unexpected(Error::IoFailure);
  }

  MemoryInfo info{};
  line_buf_.clear();

  // We only need a handful of keys; stop early once we have them.
  unsigned found = 0;
  constexpr unsigned needed = 7;

  while (found < needed && std::getline(ifs, line_buf_)) {
    const auto colon = line_buf_.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    const std::string_view key(line_buf_.data(), colon);
    // Skip whitespace after colon.
    std::size_t val_start = colon + 1;
    while (val_start < line_buf_.size() &&
           (line_buf_[val_start] == ' ' || line_buf_[val_start] == '\t')) {
      ++val_start;
    }
    // Value is digits until first non-digit.
    std::size_t val_end = val_start;
    while (val_end < line_buf_.size() && line_buf_[val_end] >= '0' &&
           line_buf_[val_end] <= '9') {
      ++val_end;
    }
    if (val_end == val_start) {
      continue;
    }

    std::uint64_t value = 0;
    if (!parse_uint(
            std::string_view(line_buf_.data() + val_start, val_end - val_start),
            value)) {
      continue;
    }

    if (key == "MemTotal") {
      info.total_kib = value;
      ++found;
    } else if (key == "MemFree") {
      info.free_kib = value;
      ++found;
    } else if (key == "MemAvailable") {
      info.available_kib = value;
      ++found;
    } else if (key == "Buffers") {
      info.buffers_kib = value;
      ++found;
    } else if (key == "Cached") {
      info.cached_kib = value;
      ++found;
    } else if (key == "SwapTotal") {
      info.swap_total_kib = value;
      ++found;
    } else if (key == "SwapFree") {
      info.swap_free_kib = value;
      ++found;
    }
  }

  if (info.total_kib == 0) {
    return std::unexpected(Error::ParseFailure);
  }
  // MemAvailable may be missing on very old kernels; fall back.
  if (info.available_kib == 0) {
    info.available_kib = info.free_kib + info.buffers_kib + info.cached_kib;
  }
  return info;
}

Result<std::vector<ThermalReading>> Monitor::read_thermals() noexcept {
  std::vector<ThermalReading> readings;
  const fs::path base{"/sys/class/thermal"};

  std::error_code ec;
  if (!fs::exists(base, ec) || !fs::is_directory(base, ec)) {
    return std::unexpected(Error::NotAvailable);
  }

  for (const auto &entry : fs::directory_iterator(base, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_directory(ec)) {
      continue;
    }

    const auto &p = entry.path();
    const auto name = p.filename().string();
    if (name.rfind("thermal_zone", 0) != 0) {
      continue;
    }

    ThermalReading r;
    // type
    {
      std::ifstream tfs(p / "type");
      if (tfs) {
        std::getline(tfs, r.type);
        // trim trailing whitespace/newline already handled by getline
      }
    }
    // temp (millidegrees)
    {
      std::ifstream tfs(p / "temp");
      if (tfs) {
        line_buf_.clear();
        if (std::getline(tfs, line_buf_)) {
          std::int32_t mC = 0;
          if (parse_int(line_buf_, mC)) {
            r.temp_mC = mC;
          } else {
            r.temp_mC = -1;
          }
        }
      } else {
        r.temp_mC = -1;
      }
    }

    if (!r.type.empty() && r.temp_mC >= 0) {
      readings.push_back(std::move(r));
    }
  }

  if (readings.empty()) {
    return std::unexpected(Error::NotAvailable);
  }
  return readings;
}

std::optional<double> Monitor::cpu_temperature_celsius() noexcept {
  auto res = read_thermals();
  if (!res) {
    return std::nullopt;
  }

  // Preference order for package / CPU temperature.
  static constexpr std::string_view preferred[] = {
      "x86_pkg_temp", "cpu-thermal", "CPU", "soc-thermal", "acpitz"};

  for (const auto pref : preferred) {
    for (const auto &r : *res) {
      if (r.type == pref) {
        return r.celsius();
      }
    }
  }

  // Fallback: first valid reading.
  if (!res->empty()) {
    return res->front().celsius();
  }
  return std::nullopt;
}

} // namespace sysmon

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sysmon {

/// Fixed-size CPU time counters from /proc/stat (jiffies / USER_HZ).
/// Order matches kernel: user, nice, system, idle, iowait, irq, softirq, steal,
/// guest, guest_nice
struct CpuTimes {
  std::uint64_t user{0};
  std::uint64_t nice{0};
  std::uint64_t system{0};
  std::uint64_t idle{0};
  std::uint64_t iowait{0};
  std::uint64_t irq{0};
  std::uint64_t softirq{0};
  std::uint64_t steal{0};
  std::uint64_t guest{0};
  std::uint64_t guest_nice{0};

  [[nodiscard]] constexpr std::uint64_t idle_all() const noexcept {
    return idle + iowait;
  }

  [[nodiscard]] constexpr std::uint64_t total() const noexcept {
    return user + nice + system + idle + iowait + irq + softirq + steal;
    // guest and guest_nice are already included in user/nice on modern kernels
  }

  [[nodiscard]] constexpr std::uint64_t busy() const noexcept {
    return total() - idle_all();
  }
};

/// Snapshot of aggregate + per-core CPU times.
struct CpuSnapshot {
  CpuTimes aggregate{};
  std::vector<CpuTimes> cores; // empty if only aggregate requested
};

/// Memory statistics in kibibytes (from /proc/meminfo).
struct MemoryInfo {
  std::uint64_t total_kib{0};
  std::uint64_t free_kib{0};
  std::uint64_t available_kib{0};
  std::uint64_t buffers_kib{0};
  std::uint64_t cached_kib{0};
  std::uint64_t swap_total_kib{0};
  std::uint64_t swap_free_kib{0};

  /// Used = total - available (preferred modern metric).
  [[nodiscard]] constexpr std::uint64_t used_kib() const noexcept {
    return total_kib > available_kib ? total_kib - available_kib : 0;
  }

  [[nodiscard]] constexpr double usage_percent() const noexcept {
    if (total_kib == 0) {
      return 0.0;
    }
    return 100.0 * static_cast<double>(used_kib()) /
           static_cast<double>(total_kib);
  }

  [[nodiscard]] constexpr double swap_usage_percent() const noexcept {
    if (swap_total_kib == 0) {
      return 0.0;
    }
    const auto used =
        swap_total_kib > swap_free_kib ? swap_total_kib - swap_free_kib : 0;
    return 100.0 * static_cast<double>(used) /
           static_cast<double>(swap_total_kib);
  }
};

/// Single thermal zone reading (millidegrees Celsius).
struct ThermalReading {
  std::string type;        // e.g. "x86_pkg_temp", "acpitz"
  std::int32_t temp_mC{0}; // millidegrees; negative if unavailable

  [[nodiscard]] constexpr double celsius() const noexcept {
    return static_cast<double>(temp_mC) / 1000.0;
  }
};

/// Error category for sysmon operations.
enum class Error { IoFailure, ParseFailure, NotAvailable, InvalidArgument };

[[nodiscard]] constexpr std::string_view to_string(Error e) noexcept {
  switch (e) {
  case Error::IoFailure:
    return "I/O failure";
  case Error::ParseFailure:
    return "parse failure";
  case Error::NotAvailable:
    return "not available";
  case Error::InvalidArgument:
    return "invalid argument";
  }
  return "unknown";
}

template <typename T> using Result = std::expected<T, Error>;

/// High-performance, allocation-conscious system metrics reader (Linux).
///
/// Design goals:
/// - Zero dynamic allocation on the hot path after first use (reuses buffers).
/// - Minimal syscalls / file opens (reuses open file descriptors where
/// practical).
/// - Leak-free: RAII, no raw new/delete, no owning raw pointers.
/// - C++23: std::expected, std::string_view, constexpr where useful.
class Monitor {
public:
  Monitor() = default;
  ~Monitor() = default;

  Monitor(const Monitor &) = delete;
  Monitor &operator=(const Monitor &) = delete;
  Monitor(Monitor &&) noexcept = default;
  Monitor &operator=(Monitor &&) noexcept = default;

  /// Read current aggregate (+ optional per-core) CPU times.
  /// Prefer calling twice with a sleep between samples to compute utilization.
  [[nodiscard]] Result<CpuSnapshot>
  read_cpu(bool include_cores = false) noexcept;

  /// Compute utilization [0.0, 100.0] between two snapshots (same core count).
  /// Returns aggregate usage; if cores present in both, also fills per-core.
  [[nodiscard]] static Result<double>
  cpu_usage_percent(const CpuSnapshot &prev, const CpuSnapshot &curr) noexcept;

  /// Per-core usage percentages (same length as cores in snapshots).
  [[nodiscard]] static Result<std::vector<double>>
  cpu_core_usage_percent(const CpuSnapshot &prev,
                         const CpuSnapshot &curr) noexcept;

  /// Read memory info from /proc/meminfo.
  [[nodiscard]] Result<MemoryInfo> read_memory() noexcept;

  /// Discover and read all thermal zones under /sys/class/thermal.
  /// Prefer "x86_pkg_temp" or "cpu-thermal" when present; otherwise first zone.
  [[nodiscard]] Result<std::vector<ThermalReading>> read_thermals() noexcept;

  /// Convenience: best-effort CPU package temperature in Celsius.
  /// Returns nullopt if no suitable sensor found.
  [[nodiscard]] std::optional<double> cpu_temperature_celsius() noexcept;

private:
  // Reusable parse buffers to avoid allocations on repeated calls.
  std::string line_buf_;
  std::string path_buf_;
};

} // namespace sysmon

#pragma once

#include "sysmon.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

namespace sysmon {

/// Latest snapshot published by the background thread.
struct Metrics {
  double cpu_percent{0.0};
  double mem_percent{0.0};
  double mem_used_mib{0.0};
  double mem_total_mib{0.0};
  std::optional<double> temp_celsius;
  std::chrono::steady_clock::time_point timestamp{};
};

/// Background sampler. Start once, stop on destruction (or explicit stop()).
class BackgroundMonitor {
public:
  explicit BackgroundMonitor(
      std::chrono::milliseconds interval = std::chrono::milliseconds{1000})
      : interval_(interval) {
    // Take the first CPU sample before the thread starts so the first
    // published value is already a real delta.
    if (auto snap = mon_.read_cpu(false)) {
      prev_ = std::move(*snap);
    }
    worker_ = std::thread([this] { run(); });
  }

  ~BackgroundMonitor() { stop(); }

  BackgroundMonitor(const BackgroundMonitor &) = delete;
  BackgroundMonitor &operator=(const BackgroundMonitor &) = delete;

  void stop() noexcept {
    if (running_.exchange(false)) {
      if (worker_.joinable()) {
        worker_.join();
      }
    }
  }

  /// Thread-safe copy of the most recent metrics.
  [[nodiscard]] Metrics get() const {
    std::lock_guard lock(mutex_);
    return latest_;
  }

private:
  void run() {
    while (running_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(interval_);

      Metrics m;
      m.timestamp = std::chrono::steady_clock::now();

      // CPU (delta against previous sample)
      if (auto curr = mon_.read_cpu(false)) {
        if (auto pct = Monitor::cpu_usage_percent(prev_, *curr)) {
          m.cpu_percent = *pct;
        }
        prev_ = std::move(*curr);
      }

      // Memory
      if (auto mem = mon_.read_memory()) {
        m.mem_percent = mem->usage_percent();
        m.mem_used_mib = mem->used_kib() / 1024.0;
        m.mem_total_mib = mem->total_kib / 1024.0;
      }

      // Temperature
      m.temp_celsius = mon_.cpu_temperature_celsius();

      {
        std::lock_guard lock(mutex_);
        latest_ = m;
      }
    }
  }

  Monitor mon_;
  CpuSnapshot prev_{};
  Metrics latest_{};
  mutable std::mutex mutex_;
  std::chrono::milliseconds interval_;
  std::atomic<bool> running_{true};
  std::thread worker_;
};

} // namespace sysmon

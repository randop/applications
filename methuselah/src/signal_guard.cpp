#include "methuselah/signal_guard.hpp"

#include <csignal>

namespace methuselah {
namespace {

volatile sig_atomic_t g_signal_received = 0;
struct sigaction g_prev_int{};
struct sigaction g_prev_term{};
bool g_installed = false;

void handle_signal(int signum) { g_signal_received = signum; }

} // namespace

SignalGuard::SignalGuard() {
  struct sigaction sa{};
  sa.sa_handler = handle_signal;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, &g_prev_int);
  sigaction(SIGTERM, &sa, &g_prev_term);
  g_installed = true;
}

SignalGuard::~SignalGuard() {
  if (!g_installed) {
    return;
  }
  sigaction(SIGINT, &g_prev_int, nullptr);
  sigaction(SIGTERM, &g_prev_term, nullptr);
  g_installed = false;
}

bool SignalGuard::received() noexcept { return g_signal_received != 0; }

int SignalGuard::signum() noexcept {
  return static_cast<int>(g_signal_received);
}

} // namespace methuselah

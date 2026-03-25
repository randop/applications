#include <cstdio>
#include <cinttypes>
#include <cstring>

extern "C" {
  // WASI‑compatible entry point
  void _start() {
    std::printf("%s,%s,%s\n", "Project", "sardis", "_start()");
    std::fflush(stdout);
  }
}

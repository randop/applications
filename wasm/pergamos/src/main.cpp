#include <cstdio>
#include <cinttypes>
#include <cstring>
#include <string_view>
#include <array>
#include <optional>
#include <iostream>

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
      std::printf("%s\n", line.c_str());
      std::fflush(stdout);
    }
    std::printf("%s,%s,%s\n", "Project", "Pergamos", "_start()");
    std::fflush(stdout);
    std::fflush(stderr);
  }
}

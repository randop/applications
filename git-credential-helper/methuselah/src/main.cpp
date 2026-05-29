#include "config.h"
#include "hack_font.h"
#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <sys/file.h>
#include <unistd.h>

const char *LOCK_FILE = "/tmp/" PROJECT_NAME ".lock";

static volatile sig_atomic_t g_signal_received = 0;

void SignalHandler(int signum) { g_signal_received = signum; }

void Cleanup() {
  unlink(LOCK_FILE);
  if (IsWindowReady()) {
    CloseWindow();
  }
}

bool CheckSingleInstance() {
  int fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
  if (fd < 0) {
    return true;
  }

  if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
    return false;
  } else {
    close(fd);
    system("wmctrl -a \"" PROJECT_NAME "\"");
    std::cerr << "Application is already running. Focused existing instance."
              << std::endl;
    return true;
  }
}

int main(void) {
  struct sigaction sa{};
  sa.sa_handler = SignalHandler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  std::atexit(Cleanup);

  if (CheckSingleInstance()) {
    return 0;
  }

  SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
  InitWindow(800, 600, PROJECT_NAME);

  if (!IsWindowReady()) {
    std::cerr << "ERROR: Failed to initialize window" << std::endl;
    return 1;
  }

  int mon = GetCurrentMonitor();
  float screenWidth = static_cast<float>(GetMonitorWidth(mon));
  float screenHeight = static_cast<float>(GetMonitorHeight(mon));

  SetWindowPosition(0, 0);
  SetWindowSize(static_cast<int>(screenWidth), static_cast<int>(screenHeight));

  SetTargetFPS(60);
  SetWindowTitle(PROJECT_NAME);

  constexpr int FONT_BASE_SIZE = 48;
  Font hackFont = LoadFontFromMemory(".ttf", HACK_REGULAR_TTF,
                                     static_cast<int>(HACK_REGULAR_TTF_SIZE),
                                     FONT_BASE_SIZE, nullptr, 0);
  SetTextureFilter(hackFont.texture, TEXTURE_FILTER_BILINEAR);
  GenTextureMipmaps(&hackFont.texture);
  SetTextureFilter(hackFont.texture, TEXTURE_FILTER_TRILINEAR);

  GuiSetFont(hackFont);
  GuiSetStyle(DEFAULT, TEXT_SIZE, 24);
  GuiSetStyle(DEFAULT, TEXT_PADDING, 12);

  bool showMessageBox = false;
  bool exitRequested = false;

  while (!WindowShouldClose() && !exitRequested && !g_signal_received) {
    if (IsKeyPressed(KEY_ESCAPE)) {
      showMessageBox = true;
    }

    BeginDrawing();
    ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

    DrawTextEx(hackFont, PROJECT_NAME, {40.0f, 40.0f}, 40.0f, 1.0f, DARKBLUE);

    GuiPanel(
        (Rectangle){screenWidth / 2 - 300, screenHeight / 2 - 180, 600, 360},
        "Raygui Demo");

    GuiLabel(
        (Rectangle){screenWidth / 2 - 260, screenHeight / 2 - 120, 200, 30},
        "This is a fullscreen application");

    if (GuiButton(
            (Rectangle){screenWidth / 2 - 100, screenHeight / 2 - 50, 200, 50},
            "Show Message")) {
      showMessageBox = true;
    }

    if (GuiButton(
            (Rectangle){screenWidth / 2 - 100, screenHeight / 2 + 30, 200, 50},
            "Exit Application")) {
      exitRequested = true;
    }

    DrawFPS(screenWidth - 120, 20);

    EndDrawing();

    if (showMessageBox) {
      int result = GuiMessageBox(
          (Rectangle){screenWidth / 2 - 200, screenHeight / 2 - 100, 400, 200},
          "#191#Exit Application", "Are you sure you want to exit?", "Yes;No");

      if (result == 1) {
        exitRequested = true;
      }
      if (result != 0) {
        showMessageBox = false;
      }
    }
  }

  if (g_signal_received) {
    std::cerr << "\nReceived signal " << g_signal_received << ", cleaning up..."
              << std::endl;
  }

  UnloadFont(hackFont);
  return 0;
}

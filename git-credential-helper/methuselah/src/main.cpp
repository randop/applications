#include "config.h"
#include "hack_font.h"
#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <sys/file.h>
#include <unistd.h>

static void RaylibLogCallback(int logLevel, const char *text, va_list args) {
  const char *prefix = nullptr;
  switch (logLevel) {
  case LOG_TRACE:
    prefix = "TRACE";
    break;
  case LOG_DEBUG:
    prefix = "DEBUG";
    break;
  case LOG_INFO:
    prefix = "INFO";
    break;
  case LOG_WARNING:
    prefix = "WARN";
    break;
  case LOG_ERROR:
    prefix = "ERROR";
    break;
  case LOG_FATAL:
    prefix = "FATAL";
    break;
  default:
    prefix = "?";
    break;
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tm_buf;
  gmtime_r(&ts.tv_sec, &tm_buf);
  char timebuf[32];
  std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
  std::fprintf(stderr, "%s.%03ldZ [%s] ", timebuf, ts.tv_nsec / 1'000'000L,
               prefix);
  std::vfprintf(stderr, text, args);
  std::fputc('\n', stderr);
}

const char *LOCK_FILE = "/tmp/" PROJECT_NAME ".lock";

static volatile sig_atomic_t g_signal_received = 0;

void SignalHandler(int signum) { g_signal_received = signum; }

void Cleanup() {
  std::cerr << "executing cleanup() ..." << std::endl;
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

  SetTraceLogCallback(RaylibLogCallback);

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

  bool exitRequested = false;
  char passwordBuf[256] = {};
  bool passwordConfirmed = false;

  while (!WindowShouldClose() && !exitRequested && !g_signal_received) {
    if (IsKeyPressed(KEY_ESCAPE)) {
      exitRequested = true;
    }
    if (IsKeyPressed(KEY_ENTER) && !passwordConfirmed) {
      passwordConfirmed = true;
      exitRequested = true;
    }

    BeginDrawing();
    ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

    DrawTextEx(hackFont, PROJECT_DESCRIPTION, {40.0f, 40.0f}, 40.0f, 1.0f,
               DARKBLUE);

    GuiPanel(
        (Rectangle){screenWidth / 2 - 300, screenHeight / 2 - 180, 600, 360},
        "GPG");

    GuiLabel(
        (Rectangle){screenWidth / 2 - 200, screenHeight / 2 - 120, 400, 30},
        "Enter passphrase:");

    GuiSetStyle(TEXTBOX, TEXT_PADDING, 8);
    {
      static bool tbEditMode = true;
      if (GuiTextBox((Rectangle){screenWidth / 2 - 200, screenHeight / 2 - 70,
                                 400, 40},
                     passwordBuf, static_cast<int>(sizeof(passwordBuf)),
                     tbEditMode)) {
        tbEditMode = !tbEditMode;
      }
    }

    if (GuiButton(
            (Rectangle){screenWidth / 2 - 210, screenHeight / 2 + 20, 190, 44},
            "Continue")) {
      passwordConfirmed = true;
      exitRequested = true;
    }

    if (GuiButton(
            (Rectangle){screenWidth / 2 + 20, screenHeight / 2 + 20, 190, 44},
            "Cancel")) {
      exitRequested = true;
    }

    DrawFPS(screenWidth - 120, 20);

    EndDrawing();
  }

  if (g_signal_received) {
    std::cerr << "\nReceived signal " << g_signal_received << ", cleaning up..."
              << std::endl;
  }

  UnloadFont(hackFont);
  return 0;
}

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
#include <filesystem>
#include <gpgme.h>
#include <iostream>
#include <pwd.h>
#include <string>
#include <sys/file.h>
#include <unistd.h>

namespace fs = std::filesystem;

fs::path get_home_path() {
  if (struct passwd *pw = getpwuid(getuid()); pw && pw->pw_dir) {
    return fs::path(pw->pw_dir);
  }
  throw std::runtime_error("Failed to get home directory");
}

fs::path resolve_path(std::string path) {
  const fs::path home = get_home_path();

  if (path.starts_with("~/")) {
    path.replace(0, 1, home.string());
  } else if (path == "~") {
    return home;
  }

  size_t pos = 0;
  while ((pos = path.find("$HOME", pos)) != std::string::npos) {
    path.replace(pos, 5, home.string());
    pos += home.string().length();
  }

  pos = 0;
  while ((pos = path.find("${HOME}", pos)) != std::string::npos) {
    path.replace(pos, 7, home.string());
    pos += home.string().length();
  }

  fs::path p(std::move(path));

  if (p.is_relative()) {
    p = fs::absolute(p);
  }

  return fs::weakly_canonical(p);
}

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

struct DecryptResult {
  std::string plaintext;
  std::string error;
};

static gpgme_error_t PassphraseCallback(void *hook, const char * /*uid_hint*/,
                                        const char * /*passphrase_info*/,
                                        int /*prev_was_bad*/, int fd) {
  const auto *passphrase = static_cast<const std::string *>(hook);
  const std::string with_newline = *passphrase + "\n";
  const char *ptr = with_newline.c_str();
  std::size_t remaining = with_newline.size();
  while (remaining > 0) {
    ssize_t written = write(fd, ptr, remaining);
    if (written < 0) {
      return gpgme_error_from_errno(errno);
    }
    ptr += written;
    remaining -= static_cast<std::size_t>(written);
  }
  return GPG_ERR_NO_ERROR;
}

static DecryptResult DecryptFile(const std::filesystem::path &gpg_path,
                                 const std::string &passphrase) {
  DecryptResult result;

  gpgme_check_version(nullptr);

  gpgme_ctx_t ctx = nullptr;
  gpgme_error_t err = gpgme_new(&ctx);
  if (err) {
    result.error = gpgme_strerror(err);
    return result;
  }

  gpgme_set_protocol(ctx, GPGME_PROTOCOL_OpenPGP);
  gpgme_set_armor(ctx, 0);
  gpgme_set_pinentry_mode(ctx, GPGME_PINENTRY_MODE_LOOPBACK);
  gpgme_set_passphrase_cb(ctx, PassphraseCallback,
                          const_cast<std::string *>(&passphrase));

  gpgme_data_t cipher = nullptr;
  err = gpgme_data_new_from_file(&cipher, gpg_path.c_str(), 1);
  if (err) {
    result.error = gpgme_strerror(err);
    gpgme_release(ctx);
    return result;
  }

  gpgme_data_t plain = nullptr;
  err = gpgme_data_new(&plain);
  if (err) {
    result.error = gpgme_strerror(err);
    gpgme_data_release(cipher);
    gpgme_release(ctx);
    return result;
  }

  err = gpgme_op_decrypt(ctx, cipher, plain);
  if (err) {
    result.error = gpgme_strerror(err);
  } else {
    gpgme_data_seek(plain, 0, SEEK_SET);
    char buf[4096];
    ssize_t n;
    while ((n = gpgme_data_read(plain, buf, sizeof(buf))) > 0) {
      result.plaintext.append(buf, static_cast<std::size_t>(n));
    }
  }

  gpgme_data_release(plain);
  gpgme_data_release(cipher);
  gpgme_release(ctx);
  return result;
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
  bool secretViewActive = false;

  while (!WindowShouldClose() && !exitRequested && !g_signal_received) {
    BeginDrawing();
    ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

    DrawTextEx(hackFont, PROJECT_DESCRIPTION, {40.0f, 40.0f}, 40.0f, 1.0f,
               DARKBLUE);

    DrawFPS(screenWidth - 120, 20);

    int result = GuiTextInputBox(
        (Rectangle){screenWidth / 2 - 300, screenHeight / 2 - 125, 600, 250},
        "GPG: <email@me.com>",
        "Enter the passphrase to     \nunlock your credentials.     ",
        "Continue;Cancel", passwordBuf, static_cast<int>(sizeof(passwordBuf)),
        &secretViewActive);

    // result: 1 = Continue, 2 = Cancel, 0 = window X closed
    if (result == 1) {
      passwordConfirmed = true;
      exitRequested = true;
    } else if (result == 0 || result == 2) {
      exitRequested = true;
    }

    EndDrawing();
  }

  if (passwordConfirmed) {
    const std::string passphrase(passwordBuf);
    explicit_bzero(passwordBuf, sizeof(passwordBuf));

    std::string input = "$HOME/.password-store/gitlab/token.gpg";

    const std::filesystem::path gpg_path = resolve_path(input);
    auto r = DecryptFile(gpg_path, "test");
    if (r.error.empty()) {
      std::cerr << "OK  " << gpg_path << std::endl
                << "plain: " << r.plaintext << std::endl;
    } else {
      std::cerr << "ERR " << gpg_path << ": " << r.error << std::endl;
    }
  } else {
    explicit_bzero(passwordBuf, sizeof(passwordBuf));
  }

  if (g_signal_received) {
    std::cerr << "\nReceived signal " << g_signal_received << ", cleaning up..."
              << std::endl;
  }

  UnloadFont(hackFont);
  return 0;
}

#include "config.h"
#include "hack_font.h"
#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "debug.hpp"

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/info_parser.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <gpgme.h>
#include <iostream>
#include <map>
#include <pwd.h>
#include <sstream>
#include <string>
#include <sys/file.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

enum PinEntryMacro { UNKNOWN, GPG, GIT };

struct Application {
  PinEntryMacro macro = UNKNOWN;
};

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

void print_args(int argc, char **argv) {
  std::cerr << "args = [";
  for (int i = 0; i < argc; ++i) {
    std::cerr << argv[i];
    if (i < argc - 1) {
      std::cerr << ", ";
    }
  }
  std::cerr << "]\n";
}

struct git_predicate {
  std::vector<std::string> capabilities;
  std::vector<std::string> wwwauth;

  std::string protocol;
  std::string host;
  std::string username;
  std::string password;

  bool hasCapability(const std::string &cap) const {
    return std::find(capabilities.begin(), capabilities.end(), cap) !=
           capabilities.end();
  }

  bool hasWwwAuth(const std::string &auth) const {
    return std::find(wwwauth.begin(), wwwauth.end(), auth) != wwwauth.end();
  }
};

git_predicate parse_git_predicate(const std::string &input) {
  git_predicate predicate;

  namespace pt = boost::property_tree;
  pt::ptree tree;

  std::istringstream iss(input);
  std::string line;

  while (std::getline(iss, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      continue;
    }

    std::string key = line.substr(0, eq_pos);
    std::string value = line.substr(eq_pos + 1);

    if (key.size() >= 2 && key.substr(key.size() - 2) == "[]") {
      key = key.substr(0, key.size() - 2);
      pt::ptree child;
      child.put("", value);
      tree.add_child(key, child);
    } else {
      tree.put(key, value);
    }
  }

  if (tree.count("capability") > 0) {
    for (const auto &item : tree.get_child("capability")) {
      predicate.capabilities.push_back(item.second.data());
    }
  }

  if (tree.count("wwwauth") > 0) {
    for (const auto &item : tree.get_child("wwwauth")) {
      predicate.wwwauth.push_back(item.second.data());
    }
  }

  predicate.protocol = tree.get("protocol", "");
  predicate.host = tree.get("host", "");
  predicate.username = tree.get("username", "");
  predicate.password = tree.get("password", "");

  return predicate;
}

void handoff_git_credentials(const git_predicate &predicate,
                             const DecryptResult &result) noexcept {
  std::cout << "protocol=" << predicate.protocol << "\nhost=" << predicate.host
            << "\nusername=" << predicate.username
            << "\npassword=" << result.plaintext << std::endl;
}

int main(int argc, char **argv) {
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

  Application app;

  if (argc >= 2) {
    if (strcmp(argv[1], "--macro=git") == 0) {
      app.macro = GIT;
      std::cerr << "info: git macro" << std::endl;
    } else if (strcmp(argv[1], "--macro=gpg") == 0) {
      app.macro = GPG;
      std::cerr << "info: gpg macro" << std::endl;
    }
  }

  if (app.macro == UNKNOWN) {
    std::cerr << "Error: unknown macro" << std::endl;
    exit(0);
  }

  if (argc >= 3 && app.macro == GIT && strcmp(argv[2], "store") == 0) {
    exit(0);
  }

  std::string gpg_file{};
  git_predicate predicate;

  if (app.macro == GIT) {
    std::string line{};
    std::string inputs{};

    while (std::getline(std::cin, line)) {
      if (line.empty()) {
        break;
      }
      if (!inputs.empty()) {
        inputs.append("\n");
      }
      inputs.append(line);
    }

    predicate = parse_git_predicate(inputs);
  }

  std::string config_file = "$HOME/.config/pinentry.methuselah";
  const std::filesystem::path config_path = resolve_path(config_file);

  if (std::filesystem::exists(config_path)) {
    std::cerr << "config path good: " << config_path.string() << std::endl;
  } else {
    std::cerr << "ERROR(configuration): config path is missing on "
              << config_path.string() << std::endl;
    exit(1);
  }

  boost::property_tree::ptree pt;

  try {
    boost::property_tree::ini_parser::read_ini(config_path.string(), pt);
  } catch (const std::exception &ex) {
    std::cerr << "Failed to parse config file: " << ex.what() << '\n';
    exit(1);
  }

  for (const auto &[host, subtree] : pt) {
    if (app.macro == GIT && boost::algorithm::iequals(host, predicate.host)) {
      gpg_file = subtree.get<std::string>("gpgfile", "");
      break;
    } else if (app.macro == GPG && boost::algorithm::iequals(host, "gpg")) {
      gpg_file = subtree.get<std::string>("gpgfile", "");
      break;
    }
  }

  if (gpg_file.empty()) {
    std::cerr << "ERROR(configuration): Missing gpg file on "
              << config_path.string() << std::endl;
    exit(1);
  }

  const std::filesystem::path gpg_path = resolve_path(gpg_file);
  gpg_file = gpg_path.string();

  if (app.macro == GIT) {
    std::cerr << "git username: " << predicate.username << std::endl;
    std::cerr << "git gpgfile: " << gpg_file << std::endl;
  }

  if (std::filesystem::exists(gpg_path)) {
    std::cerr << "gpg: OK" << std::endl;
  } else {
    exit(1);
  }

  // try stored gpg agent
  auto remember_result = DecryptFile(gpg_path, "");
  if (remember_result.error.empty()) {
    if (app.macro == GIT) {
      handoff_git_credentials(predicate, remember_result);
    }
    exit(0);
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
  bool secretViewActive = false;

  while (!WindowShouldClose() && !exitRequested && !g_signal_received) {
    BeginDrawing();
    ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));

    DrawTextEx(hackFont, PROJECT_DESCRIPTION, {40.0f, 40.0f}, 40.0f, 1.0f,
               DARKBLUE);

    DrawFPS(screenWidth - 120, 20);

    int result = GuiTextInputBox(
        (Rectangle){screenWidth / 2 - 300, screenHeight / 2 - 125, 600, 250},
        "GPG: <email@maildomain.ngo>",
        "Enter the passphrase to     \nunlock your credentials.     ",
        "Continue;Cancel", passwordBuf, static_cast<int>(sizeof(passwordBuf)),
        &secretViewActive);

    // result: 1 = Continue, 2 = Cancel, 0 = window X closed
    if (result == 1) {
      const std::string passphrase(passwordBuf);
      explicit_bzero(passwordBuf, sizeof(passwordBuf));
      auto decrypt_result = DecryptFile(gpg_path, passphrase);
      if (decrypt_result.error.empty()) {
        if (app.macro == GIT) {
          handoff_git_credentials(predicate, decrypt_result);
        } else if (app.macro == GPG) {
          std::cerr << "gpg successfully processed file: "
                    << config_path.string() << std::endl;
        }
        exitRequested = true;
      } else {
        std::cerr << "ERROR: credential decryption issue on " << gpg_path
                  << ": " << decrypt_result.error << std::endl;
      }
    } else if (result == 0 || result == 2) {
      exitRequested = true;
    }

    EndDrawing();
  }

  explicit_bzero(passwordBuf, sizeof(passwordBuf));

  if (g_signal_received) {
    std::cerr << "\nReceived signal " << g_signal_received << ", cleaning up..."
              << std::endl;
  }

  UnloadFont(hackFont);
  return 0;
}

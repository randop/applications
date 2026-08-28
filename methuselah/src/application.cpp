#include "config.h"

#include "methuselah/application.hpp"

#include "methuselah/config.hpp"
#include "methuselah/git_credentials.hpp"
#include "methuselah/gpg_decrypt.hpp"
#include "methuselah/instance_lock.hpp"
#include "methuselah/logging.hpp"
#include "methuselah/macro.hpp"
#include "methuselah/path.hpp"
#include "methuselah/pinentry_server.hpp"
#include "methuselah/secure_memory.hpp"
#include "methuselah/signal_guard.hpp"
#include "methuselah/ui_session.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace methuselah {
namespace {

constexpr const char kDefaultMessage[] =
    "Enter the passphrase to     \nunlock your credentials.     ";

constexpr const char kConfigFile[] = "$HOME/.config/pinentry.methuselah";

Macro parse_macro(int argc, char **argv) {
  if (argc < 2) {
    return Macro::Unknown;
  }

  const std::string_view flag{argv[1]};
  if (flag == "--macro=git") {
    SPDLOG_TRACE("macro: git");
    return Macro::Git;
  } else if (flag == "--macro=gpg") {
    SPDLOG_TRACE("macro: gpg");
    return Macro::Gpg;
  } else if (flag == "--macro=dummy") {
    SPDLOG_TRACE("macro: dummy");
    return Macro::Dummy;
  } else if (flag == "--macro=ssh") {
    SPDLOG_TRACE("macro: ssh");
    return Macro::Ssh;
  }
  return Macro::Unknown;
}

int run_git_flow(const GitPredicate &predicate) {
  const std::filesystem::path config_path = resolve_path(kConfigFile);

  if (std::filesystem::exists(config_path)) {
    SPDLOG_TRACE("config path OK: {}", config_path.string());
  } else {
    std::cerr << "ERROR(configuration): config path is missing on "
              << config_path.string() << std::endl;
    return EXIT_FAILURE;
  }

  Config config;
  try {
    config = Config::load(config_path);
  } catch (const std::exception &ex) {
    std::cerr << "Failed to parse config file: " << ex.what() << '\n';
    return EXIT_FAILURE;
  }

  const auto gpg_file = config.gpg_file_for(predicate.host());
  if (!gpg_file) {
    std::cerr << "ERROR(configuration): Missing gpg file on "
              << config_path.string() << std::endl;
    return EXIT_FAILURE;
  }

  const std::filesystem::path gpg_path = resolve_path(*gpg_file);

  SPDLOG_TRACE("git username: {}", predicate.username());
  SPDLOG_TRACE("git gpgfile: {}", gpg_path.string());

  if (std::filesystem::exists(gpg_path)) {
    SPDLOG_TRACE("gpg: OK");
  } else {
    return EXIT_FAILURE;
  }

  GpgDecryptor decryptor;
  auto remembered = decryptor.decrypt_file(gpg_path, "");
  if (remembered.ok()) {
    predicate.write_credentials(remembered.plaintext());
    return EXIT_SUCCESS;
  }

  UiSession::install_log_callback();
  UiSession ui;
  if (!ui.ready()) {
    return EXIT_FAILURE;
  }

  PromptOptions options;
  options.title = "git credentials";
  options.message = kDefaultMessage;

  while (!SignalGuard::received()) {
    std::optional<std::string> password = ui.prompt_password(options);
    if (!password) {
      break;
    }

    auto decrypt_result = decryptor.decrypt_file(gpg_path, *password);
    secure_clear(*password);

    if (decrypt_result.ok()) {
      predicate.write_credentials(decrypt_result.plaintext());
      break;
    }

    std::cerr << "ERROR: credential decryption issue on " << gpg_path << ": "
              << decrypt_result.error() << std::endl;
  }

  if (SignalGuard::received()) {
    SPDLOG_TRACE("application received signal {}, cleaning up...",
                 SignalGuard::signum());
  }

  return EXIT_SUCCESS;
}

int run_dummy() {
  UiSession::install_debug_callback();
  UiSession ui;
  if (!ui.ready()) {
    return EXIT_FAILURE;
  }

  PromptOptions options;
  options.title = "dummy";
  options.message = kDefaultMessage;

  while (!SignalGuard::received()) {
    std::optional<std::string> password = ui.prompt_password(options);
    if (!password) {
      break;
    }
  }

  if (SignalGuard::received()) {
    SPDLOG_TRACE("application received signal {}, cleaning up...",
                 SignalGuard::signum());
  }

  return EXIT_SUCCESS;
}

int run_ssh_flow() {
  UiSession::install_log_callback();
  UiSession ui;
  if (!ui.ready()) {
    return EXIT_FAILURE;
  }

  PromptOptions options;
  options.title = "ssh agent";
  options.message = kDefaultMessage;

  while (!SignalGuard::received()) {
    std::optional<std::string> password = ui.prompt_password(options);
    if (!password) {
      break;
    }
    std::cout << *password << std::endl;
    secure_clear(*password);
    break;
  }

  if (SignalGuard::received()) {
    SPDLOG_TRACE("application received signal {}, cleaning up...",
                 SignalGuard::signum());
  }

  return EXIT_SUCCESS;
}

} // namespace

int Application::run(int argc, char **argv) {
  const std::string_view flag{argv[1]};
  if (flag == "--version") {
    version();
    return EXIT_SUCCESS;
  }

  Logger logger;
  SignalGuard signals;
  InstanceLock lock;

  if (!lock.acquired()) {
    return EXIT_SUCCESS;
  }

  const Macro macro = parse_macro(argc, argv);
  if (macro == Macro::Unknown) {
    std::cerr << "Error: unknown macro" << std::endl;
    return EXIT_FAILURE;
  }

  if (macro == Macro::Git && argc >= 3 && std::strcmp(argv[2], "store") == 0) {
    return EXIT_SUCCESS;
  } else if (macro == Macro::Gpg) {
    PinentryServer server("gpg credentials", kDefaultMessage);
    return server.run();
  } else if (macro == Macro::Git) {
    const GitPredicate predicate = GitPredicate::read_from_stdin();
    return run_git_flow(predicate);
  } else if (macro == Macro::Dummy) {
    return run_dummy();
  } else if (macro == Macro::Ssh) {
    return run_ssh_flow();
  }
  return EXIT_FAILURE;
}

void Application::version() {
  std::cout << PROJECT_NAME << " " << PROJECT_VERSION << std::endl;
}

} // namespace methuselah

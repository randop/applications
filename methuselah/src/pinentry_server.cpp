#include "methuselah/pinentry_server.hpp"

#include "methuselah/secure_memory.hpp"
#include "methuselah/signal_guard.hpp"
#include "methuselah/ui_session.hpp"

#include <assuan.h>
#include <cstdlib>
#include <gpg-error.h>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unistd.h>
#include <utility>

namespace methuselah {

class PinentryServer::Impl {
public:
  Impl(std::string title, std::string dialog_message)
      : title_(std::move(title)), dialog_message_(std::move(dialog_message)) {}

  [[nodiscard]] int run();

private:
  static Impl *self(assuan_context_t ctx) {
    return static_cast<Impl *>(assuan_get_pointer(ctx));
  }

  static gpg_error_t cmd_setdesc(assuan_context_t ctx, char *line);
  static gpg_error_t cmd_setprompt(assuan_context_t ctx, char *line);
  static gpg_error_t cmd_settitle(assuan_context_t ctx, char *line);
  static gpg_error_t cmd_seterror(assuan_context_t ctx, char *line);
  static gpg_error_t cmd_settimeout(assuan_context_t ctx, char *line);
  static gpg_error_t cmd_setrepeat(assuan_context_t ctx, char *line);
  static gpg_error_t cmd_getpin(assuan_context_t ctx, char *line);
  static gpg_error_t cmd_confirm(assuan_context_t ctx, char *line);

  gpg_error_t on_getpin(assuan_context_t ctx);

  std::string title_;
  std::string description_;
  std::string prompt_;
  std::string error_message_;
  std::string dialog_message_;
  int timeout_ = 0;
  bool repeat_ = false;
};

gpg_error_t PinentryServer::Impl::cmd_setdesc(assuan_context_t ctx,
                                              char *line) {
  self(ctx)->description_ = (line && *line) ? line : "";
  return 0;
}

gpg_error_t PinentryServer::Impl::cmd_setprompt(assuan_context_t ctx,
                                                char *line) {
  self(ctx)->prompt_ = (line && *line) ? line : "";
  return 0;
}

gpg_error_t PinentryServer::Impl::cmd_settitle(assuan_context_t ctx,
                                               char *line) {
  self(ctx)->title_ = (line && *line) ? line : "";
  return 0;
}

gpg_error_t PinentryServer::Impl::cmd_seterror(assuan_context_t ctx,
                                               char *line) {
  self(ctx)->error_message_ = (line && *line) ? line : "";
  return 0;
}

gpg_error_t PinentryServer::Impl::cmd_settimeout(assuan_context_t ctx,
                                                 char *line) {
  self(ctx)->timeout_ = line ? std::atoi(line) : 0;
  return 0;
}

gpg_error_t PinentryServer::Impl::cmd_setrepeat(assuan_context_t ctx,
                                                char * /*line*/) {
  self(ctx)->repeat_ = true;
  return 0;
}

gpg_error_t PinentryServer::Impl::cmd_getpin(assuan_context_t ctx,
                                             char * /*line*/) {
  return self(ctx)->on_getpin(ctx);
}

gpg_error_t PinentryServer::Impl::cmd_confirm(assuan_context_t /*ctx*/,
                                              char * /*line*/) {
  return 0;
}

gpg_error_t PinentryServer::Impl::on_getpin(assuan_context_t ctx) {
  UiSession ui;
  if (!ui.ready()) {
    return gpg_error(GPG_ERR_CANCELED);
  }

  PromptOptions options;
  options.title = title_;
  options.message = dialog_message_;
  options.timeout_seconds = timeout_;

  std::optional<std::string> pass = ui.prompt_password(options);
  if (!pass) {
    return gpg_error(GPG_ERR_CANCELED);
  }

  assuan_begin_confidential(ctx);
  const gpg_error_t err = assuan_send_data(ctx, pass->data(), pass->size());
  assuan_end_confidential(ctx);

  secure_clear(*pass);
  return err;
}

int PinentryServer::Impl::run() {
  UiSession::silence_trace_logs();

  assuan_context_t ctx = nullptr;
  gpg_error_t err = assuan_new(&ctx);
  if (err) {
    std::cerr << "ERROR(pinentry): assuan_new failed: " << gpg_strerror(err)
              << std::endl;
    return EXIT_FAILURE;
  }

  struct AssuanRelease {
    assuan_context_t ctx = nullptr;
    AssuanRelease() = delete;
    explicit AssuanRelease(assuan_context_t c) : ctx(c) {}
    ~AssuanRelease() {
      if (ctx != nullptr) {
        assuan_release(ctx);
      }
    }
    AssuanRelease(const AssuanRelease &) = delete;
    AssuanRelease &operator=(const AssuanRelease &) = delete;
  } guard{ctx};

  assuan_set_pointer(ctx, this);

  assuan_fd_t filedes[2] = {STDIN_FILENO, STDOUT_FILENO};
  err = assuan_init_pipe_server(ctx, filedes);
  if (err) {
    std::cerr << "ERROR(pinentry): assuan_init_pipe_server failed: "
              << gpg_strerror(err) << std::endl;
    return EXIT_FAILURE;
  }

  static const struct {
    const char *name;
    assuan_handler_t handler;
  } kCommands[] = {
      {"SETDESC", cmd_setdesc},       {"SETPROMPT", cmd_setprompt},
      {"SETTITLE", cmd_settitle},     {"SETERROR", cmd_seterror},
      {"SETTIMEOUT", cmd_settimeout}, {"SETREPEAT", cmd_setrepeat},
      {"GETPIN", cmd_getpin},         {"CONFIRM", cmd_confirm},
  };

  for (const auto &command : kCommands) {
    err = assuan_register_command(ctx, command.name, command.handler, nullptr);
    if (err) {
      std::cerr << "ERROR(pinentry): failed to register " << command.name
                << ": " << gpg_strerror(err) << std::endl;
      return EXIT_FAILURE;
    }
  }

  while (!SignalGuard::received()) {
    err = assuan_accept(ctx);
    if (err) {
      break;
    }

    err = assuan_process(ctx);
    if (err) {
      break;
    }
  }

  return EXIT_SUCCESS;
}

PinentryServer::PinentryServer(std::string title, std::string dialog_message)
    : impl_(std::make_unique<Impl>(std::move(title),
                                   std::move(dialog_message))) {}

PinentryServer::~PinentryServer() = default;

int PinentryServer::run() { return impl_->run(); }

} // namespace methuselah

#include "methuselah/ui_session.hpp"

#include "config.h"
#include "hack_font.h"
#include "methuselah/secure_memory.hpp"
#include "methuselah/signal_guard.hpp"

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include <spdlog/spdlog.h>

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <memory>

namespace methuselah {
namespace {

void raylib_log_callback(int log_level, const char *text, va_list args) {
  const char *prefix = nullptr;
  switch (log_level) {
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

  char formatted[1024];
  vsnprintf(formatted, sizeof(formatted), text, args);
  SPDLOG_TRACE("<{}> {}", prefix, formatted);
  (void)prefix;
}

} // namespace

struct UiSession::Impl {
  Font font{};
  float screen_width = 0.0f;
  float screen_height = 0.0f;
  bool ready = false;
  bool font_loaded = false;
};

UiSession::UiSession() : impl_(std::make_unique<Impl>()) {
  SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
  InitWindow(800, 600, PROJECT_NAME);

  if (!IsWindowReady()) {
    std::cerr << "ERROR: Failed to initialize window" << std::endl;
    return;
  }

  const int mon = GetCurrentMonitor();
  impl_->screen_width = static_cast<float>(GetMonitorWidth(mon));
  impl_->screen_height = static_cast<float>(GetMonitorHeight(mon));

  SetWindowPosition(0, 0);
  SetWindowSize(static_cast<int>(impl_->screen_width),
                static_cast<int>(impl_->screen_height));

  SetTargetFPS(60);
  SetWindowTitle(PROJECT_NAME);

  constexpr int kFontBaseSize = 48;
  impl_->font = LoadFontFromMemory(".ttf", HACK_REGULAR_TTF,
                                   static_cast<int>(HACK_REGULAR_TTF_SIZE),
                                   kFontBaseSize, nullptr, 0);
  SetTextureFilter(impl_->font.texture, TEXTURE_FILTER_BILINEAR);
  GenTextureMipmaps(&impl_->font.texture);
  SetTextureFilter(impl_->font.texture, TEXTURE_FILTER_TRILINEAR);
  impl_->font_loaded = true;

  GuiSetFont(impl_->font);
  GuiSetStyle(DEFAULT, TEXT_SIZE, 24);
  GuiSetStyle(DEFAULT, TEXT_PADDING, 12);

  impl_->ready = true;
}

UiSession::~UiSession() {
  if (!impl_) {
    return;
  }
  if (impl_->font_loaded) {
    UnloadFont(impl_->font);
    impl_->font_loaded = false;
  }
  if (IsWindowReady()) {
    CloseWindow();
  }
}

UiSession::UiSession(UiSession &&) noexcept = default;
UiSession &UiSession::operator=(UiSession &&) noexcept = default;

void UiSession::silence_trace_logs() { SetTraceLogLevel(LOG_NONE); }

void UiSession::install_log_callback() {
  SetTraceLogCallback(raylib_log_callback);
}

bool UiSession::ready() const noexcept { return impl_ && impl_->ready; }

std::optional<std::string>
UiSession::prompt_password(const PromptOptions &options) {
  if (!ready()) {
    return std::nullopt;
  }

  SecureArray<256> password_buf;
  bool secret_view_active = false;
  int result = 0; // 0 = open, 1 = OK, -1 = Cancel/timeout/window close
  const double start = GetTime();

  while (!WindowShouldClose() && result == 0 && !SignalGuard::received()) {
    if (options.timeout_seconds > 0 &&
        (GetTime() - start) > static_cast<double>(options.timeout_seconds)) {
      result = -1;
      break;
    }

    BeginDrawing();
    ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
    DrawTextEx(impl_->font, PROJECT_TAG, {40.0f, 40.0f}, 40.0f, 1.0f, DARKBLUE);
    DrawFPS(impl_->screen_width - 120, 20);

    const Rectangle box{impl_->screen_width / 2 - 300,
                        impl_->screen_height / 2 - 125, 600, 250};
    const int dialog_result = GuiTextInputBox(
        box, options.title.c_str(), options.message.c_str(), "Continue;Cancel",
        password_buf.data(), password_buf.size(), &secret_view_active);

    if (dialog_result == 2 || dialog_result == 0) {
      result = -1;
    } else if (dialog_result == 1) {
      result = 1;
    }

    EndDrawing();
  }

  if (result != 1) {
    return std::nullopt;
  }

  return std::string(password_buf.data());
}

} // namespace methuselah

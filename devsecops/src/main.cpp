#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "background_monitor.hpp"
#include "config.h"
#include "hack_font.h"

#include <array>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

using namespace std;

// ---------- minimal generator coroutine type ----------
template <typename T> struct Generator {
  struct promise_type {
    T current_value;
    Generator get_return_object() {
      return Generator{
          std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(T value) {
      current_value = std::move(value);
      return {};
    }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };

  std::coroutine_handle<promise_type> handle;

  explicit Generator(std::coroutine_handle<promise_type> h) : handle(h) {}
  Generator(const Generator &) = delete;
  Generator(Generator &&other) noexcept : handle(other.handle) {
    other.handle = nullptr;
  }
  ~Generator() {
    if (handle) {
      handle.destroy();
    }
  }

  bool next() {
    if (!handle || handle.done()) {
      return false;
    }
    handle.resume();
    return !handle.done();
  }

  T &value() { return handle.promise().current_value; }
};

// ---------- thread-safe circular buffer ----------
template <typename T, size_t N> class CircularBuffer {
public:
  void push(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_[head_] = std::move(value);
    head_ = (head_ + 1) % N;
    if (count_ < N) {
      ++count_;
    }
  }

  std::array<T, N> snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::array<T, N> out{};
    for (size_t i = 0; i < count_; ++i) {
      size_t idx = (head_ + N - count_ + i) % N;
      out[i] = buffer_[idx];
    }
    return out;
  }

  size_t count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
  }

private:
  std::array<T, N> buffer_{};
  size_t head_ = 0;
  size_t count_ = 0;
  std::mutex mutex_;
};

constexpr size_t BUFFER_SIZE = 20;
using LineBuffer = CircularBuffer<std::string, BUFFER_SIZE>;

// a service entry parsed from a "name,status" csv line
struct ServiceEntry {
  char name[15];
  char status[10];
};

constexpr size_t SVC_BUFFER_SIZE = 10;
using SvcBuffer = CircularBuffer<ServiceEntry, SVC_BUFFER_SIZE>;

// parses a "name,status" line into a ServiceEntry, truncating to fit the fields
ServiceEntry ParseServiceLine(const std::string &line) {
  ServiceEntry entry{};
  size_t comma = line.find(',');
  std::string namePart =
      (comma != std::string::npos) ? line.substr(0, comma) : line;
  std::string statusPart =
      (comma != std::string::npos) ? line.substr(comma + 1) : "";

  std::strncpy(entry.name, namePart.c_str(), sizeof(entry.name) - 1);
  entry.name[sizeof(entry.name) - 1] = '\0';

  std::strncpy(entry.status, statusPart.c_str(), sizeof(entry.status) - 1);
  entry.status[sizeof(entry.status) - 1] = '\0';

  return entry;
}

// coroutine that yields the current contents of the buffer, oldest first
Generator<std::string> DisplayBufferLines(LineBuffer &buffer) {
  auto snap = buffer.snapshot();
  size_t n = buffer.count();
  for (size_t i = 0; i < n; ++i) {
    co_yield snap[i];
  }
}

// background thread: tails a file, splits on '\n', pushes each parsed line into
// the buffer
template <typename T, size_t N, typename ParseFn>
void TailFileGeneric(const std::string &path, const int interval,
                     const bool eof, CircularBuffer<T, N> &buffer,
                     std::atomic<bool> &running, ParseFn parse) {
  std::ifstream file(path);
  if (!file.is_open()) {
    file.open(path);
  }

  if (eof) {
    file.seekg(0, std::ios::end); // only tail new lines from here on
  }

  std::string leftover;
  while (running.load()) {
    if (eof) {
      file.clear();             // clear eof/fail flags
      file.seekg(file.tellg()); // force a resync so the next read
                                // actually re-polls the file instead
                                // of trusting the stale eof state

      char ch;
      while (file.get(ch)) {
        if (ch == '\n') {
          buffer.push(parse(leftover));
          leftover.clear();
        } else {
          leftover += ch;
        }
      }

    } else {
      std::ifstream sfile(path);

      char ch;
      while (sfile.get(ch)) {
        if (ch == '\n') {
          buffer.push(parse(leftover));
          leftover.clear();
        } else {
          leftover += ch;
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
  }
}

void TailFile(const std::string &path, LineBuffer &buffer,
              std::atomic<bool> &running) {
  TailFileGeneric(path, 500, true, buffer, running,
                  [](const std::string &line) { return line; });
}

void TailSvcFile(const std::string &path, SvcBuffer &buffer,
                 std::atomic<bool> &running) {
  TailFileGeneric(path, 5000, false, buffer, running, ParseServiceLine);
}

/**
 * main entry point
 **/

int main() {
  using namespace std::chrono_literals;

  SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT |
                 FLAG_WINDOW_HIGHDPI);
  InitWindow(800, 600, PROJECT_DESCRIPTION);
  SetTargetFPS(60);

  constexpr int kFontBaseSize = 96;
  constexpr int kFontGlyphCount = 224;
  int codepoints[kFontGlyphCount];
  for (int i = 0; i < kFontGlyphCount; i++) {
    codepoints[i] = 32 + i;
  }
  Font font = LoadFontFromMemory(".ttf", HACK_REGULAR_TTF,
                                 static_cast<int>(HACK_REGULAR_TTF_SIZE),
                                 kFontBaseSize, codepoints, kFontGlyphCount);
  SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
  GenTextureMipmaps(&font.texture);
  SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);

  Vector2 dpiScale = GetWindowScaleDPI();
  float uiScale = dpiScale.x;

  GuiSetFont(font);
  GuiSetStyle(DEFAULT, TEXT_SIZE, (int)(16 * uiScale));
  GuiSetStyle(DEFAULT, TEXT_PADDING, 12);
  GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, ColorToInt(BLACK));

  const int current_monitor = GetCurrentMonitor();
  const int screenWidth = static_cast<int>(GetMonitorWidth(current_monitor));
  const int screenHeight = static_cast<int>(GetMonitorHeight(current_monitor));

  string header = format("{} v{} {}x{} |", PROJECT_DESCRIPTION, PROJECT_VERSION,
                         screenWidth, screenHeight);

  SetWindowPosition(0, 0);
  SetWindowSize(screenWidth, screenHeight);

  LineBuffer lineBuffer;
  SvcBuffer svcBuffer;
  std::atomic<bool> running{true};
  std::thread tailThread(TailFile, "/tmp/log.txt", std::ref(lineBuffer),
                         std::ref(running));
  std::thread svcTailThread(TailSvcFile, "/tmp/svc.txt", std::ref(svcBuffer),
                            std::ref(running));

  constexpr int rectWidth = 200;
  constexpr int rectHeight = 50;
  constexpr int rectSpacing = 15;
  constexpr int rectStartX = 10;
  constexpr int rectStartY = 70;

  sysmon::BackgroundMonitor bg{3000ms};

  string hw_mon_value{};

  Color hwColor = BLUE;

  while (!WindowShouldClose()) {
    auto svcSnapshot = svcBuffer.snapshot();
    size_t rectCount = svcBuffer.count();

    auto hw_mon = bg.get();
    hw_mon_value = format(
        "{} cpu={:.1f}% ({}) mem={:.1f}%\n", header, hw_mon.cpu_percent,
        hw_mon.temp_celsius ? format("{:.1f}°C", *hw_mon.temp_celsius) : "n/a",
        hw_mon.mem_percent);

    const bool high_cpu = hw_mon.cpu_percent >= 50.0;
    const bool high_temp = hw_mon.temp_celsius && *hw_mon.temp_celsius >= 50.0;
    const bool high_mem = hw_mon.mem_percent >= 50.0;

    if (high_cpu || high_temp || high_mem) {
      hwColor = RED;
    } else {
      hwColor = BLUE;
    }

    BeginDrawing();
    ClearBackground(RAYWHITE);

    DrawTextEx(font, hw_mon_value.c_str(), {5.0f, 10.0f}, 27, 1, hwColor);

    for (size_t i = 0; i < rectCount; ++i) {
      if (strcmp(svcSnapshot[i].name, "NULL") == 0) {
        continue;
      }
      Rectangle rect = {(float)rectStartX,
                        (float)rectStartY + i * (rectHeight + rectSpacing),
                        (float)rectWidth, (float)rectHeight};

      Color statusBg = GREEN;
      Color statusText = WHITE;
      if (strcmp(svcSnapshot[i].status, "BAD") == 0) {
        statusBg = YELLOW;
        statusText = RED;
      }
      DrawRectangleRec(rect, statusBg);
      DrawRectangleLinesEx(rect, 2, BLACK);
      DrawTextEx(font, svcSnapshot[i].name,
                 {(float)rect.x + 4, (float)rect.y + 15}, 20, 1, BLACK);
      DrawTextEx(font, svcSnapshot[i].status,
                 {(float)rect.x + (float)rectWidth - 55, (float)rect.y + 10},
                 30, 1, statusText);
    }

    float y = 50;
    auto gen = DisplayBufferLines(lineBuffer);
    while (gen.next()) {
      GuiLabel({rectWidth + rectStartX + 20.0f, y, screenWidth * 1.0f, 30},
               gen.value().c_str());
      y += 35;
    }
    DrawFPS(screenWidth - 90, 10);
    EndDrawing();
  }

  running = false;
  tailThread.join();

  CloseWindow();
  return 0;
}

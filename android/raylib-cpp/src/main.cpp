#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "hack_font.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

// ---------------------------------------------------------
// definitions
// ---------------------------------------------------------
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

// ---------------------------------------------------------
// Android soft-keyboard helpers
// ---------------------------------------------------------
#if defined(PLATFORM_ANDROID)

#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>

extern "C" struct android_app *GetAndroidApp(void);

#include <mutex>
#include <queue>

static std::mutex gCharMutex;
static std::queue<int> gCharQueue;

extern "C" {

JNIEXPORT void JNICALL Java_com_example_raylibcpp_NativeLoader_nativePushChar(
    JNIEnv *, jobject, jint codepoint) {
  std::lock_guard<std::mutex> lock(gCharMutex);
  gCharQueue.push(codepoint);
}

JNIEXPORT void JNICALL Java_com_example_raylibcpp_NativeLoader_nativePushKey(
    JNIEnv *, jobject, jint key) {
  // We only care about Backspace and Enter for now
  if (key == 259) { // KEY_BACKSPACE
    std::lock_guard<std::mutex> lock(gCharMutex);
    gCharQueue.push(8);    // ASCII backspace
  } else if (key == 257) { // KEY_ENTER
    std::lock_guard<std::mutex> lock(gCharMutex);
    gCharQueue.push(10); // ASCII newline / enter
  }
}

} // extern "C"

static void AndroidShowSoftKeyboard(bool show) {
  struct android_app *app = GetAndroidApp();
  if (!app || !app->activity)
    return;

  JavaVM *vm = app->activity->vm;
  JNIEnv *env = nullptr;

  if (vm->AttachCurrentThread(&env, nullptr) != 0 || !env)
    return;

  jobject activity = app->activity->clazz;
  jclass cls = env->GetObjectClass(activity);
  if (!cls) {
    vm->DetachCurrentThread();
    return;
  }

  jmethodID mid =
      env->GetMethodID(cls, show ? "showSoftInput" : "hideSoftInput", "()V");

  if (mid) {
    env->CallVoidMethod(activity, mid);
  } else {
    __android_log_print(
        ANDROID_LOG_ERROR, "raylib",
        "Method %s not found – did you add it to NativeLoader.java?",
        show ? "showSoftInput" : "hideSoftInput");
  }

  env->DeleteLocalRef(cls);
  vm->DetachCurrentThread();
}

#else
static void AndroidShowSoftKeyboard(bool) {}
#endif

int main() {
  // ---------------------------------------------------------
  // NOTE: FLAG_WINDOW_HIGHDPI removed.
  //
  // On Android, GetScreenWidth()/GetScreenHeight() already
  // report physical pixels, so this flag isn't needed. When
  // it's set, BeginScissorMode() applies an extra DPI-scale
  // step to the scissor rect that doesn't line up with the
  // raw pixel coordinates used to draw the list content.
  // ---------------------------------------------------------
  SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);

  InitWindow(0, 0, "RGUI Application");

  SetTargetFPS(60);
  SetExitKey(0);

  // ---------------------------------------------------------
  // Hack font
  // ---------------------------------------------------------

  constexpr int kFontBaseSize = 96;
  constexpr int kFontGlyphCount = 224;

  int codepoints[kFontGlyphCount];
  for (int i = 0; i < kFontGlyphCount; ++i)
    codepoints[i] = 32 + i;

  Font font = LoadFontFromMemory(".ttf", HACK_REGULAR_TTF,
                                 static_cast<int>(HACK_REGULAR_TTF_SIZE),
                                 kFontBaseSize, codepoints, kFontGlyphCount);

  SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
  GenTextureMipmaps(&font.texture);
  SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);

  GuiSetFont(font);
  GuiSetStyle(DEFAULT, TEXT_SIZE, 32);
  GuiSetStyle(DEFAULT, TEXT_PADDING, 14);

  // ---------------------------------------------------------
  // Items
  // ---------------------------------------------------------

  const std::vector<std::string> items = {
      "Olivia Bennett",      "Liam Carter",       "Emma Rodriguez",
      "Noah Mitchell",       "Ava Thompson",      "Elijah Foster",
      "Sophia Ramirez",      "James Coleman",     "Isabella Reyes",
      "Benjamin Hughes",     "Mia Sanders",       "Lucas Powell",
      "Charlotte Bailey",    "Henry Ward",        "Amelia Torres",
      "Alexander Price",     "Harper Nguyen",     "Sebastian Long",
      "Evelyn Ross",         "Jack Patterson",    "Abigail Simmons",
      "Owen Flores",         "Emily Hendricks",   "Daniel Griffin",
      "Elizabeth Watkins",   "Matthew Barnes",    "Sofia Delgado",
      "Michael Chambers",    "Avery Stone",       "David Ferguson",
      "Ella Whitfield",      "Joseph Marsh",      "Scarlett Vaughn",
      "Samuel Kirby",        "Grace Holloway",    "Wyatt Osborne",
      "Chloe Vasquez",       "John Abernathy",    "Victoria Merritt",
      "Carter Lindgren",     "Riley Fitzgerald",  "Julian Navarro",
      "Zoey Whitaker",       "Levi Sinclair",     "Nora Beaumont",
      "Isaac Winslow",       "Hannah Castillo",   "Christopher Doyle",
      "Lily Ashworth",       "Andrew Pemberton",  "Layla Contreras",
      "Joshua Fairweather",  "Aria Blackwood",    "Ryan Emerson",
      "Ellie Sutherland",    "Nathan Prescott",   "Natalie Hartman",
      "Caleb Donovan",       "Zoe Rutherford",    "Dylan Ashby",
      "Leah Kensington",     "Gabriel Thorne",    "Audrey Callahan",
      "Anthony Wexford",     "Savannah Lockhart", "Christian Radcliffe",
      "Brooklyn Ellison",    "Isaiah Montague",   "Claire Fenwick",
      "Adrian Blackburn",    "Skylar Winters",    "Eli Harrington",
      "Paisley Kingsley",    "Jordan Ashcroft",   "Aubrey Sheffield",
      "Charles Bellweather", "Genesis Alvarado",  "Colton Whitmore",
      "Kennedy Farrow",      "Aaron Redgrave",    "Piper Castellano",
      "Ezra Thackeray",      "Violet Marchetti",  "Hunter Delacroix",
      "Willow Sandoval",     "Landon Pemberly",   "Autumn Kowalski",
      "Jonathan Vance",      "Ruby Escobar",      "Ian Sorensen",
      "Hazel Whitlock",      "Cameron Duarte",    "Stella Beaumont",
      "Robert Achebe",       "Ivy Okafor",        "Thomas Nakamura",
      "Luna Yamamoto",       "William Choudhury", "Nova Petrossian",
      "George Alcantara"};

  const int totalItemCount = static_cast<int>(items.size());

  constexpr float itemHeight = 100.0f;
  constexpr float textSize = 60.0f;

  // ---------------------------------------------------------
  // Search state
  // ---------------------------------------------------------

  constexpr int kSearchBufferSize = 64;
  char searchBuffer[kSearchBufferSize] = "";
  bool searchEditMode = false;
  bool prevSearchEditMode = false;

  // ---------------------------------------------------------
  // Scroll state
  // ---------------------------------------------------------

  float scrollY = 0.0f;
  float velocityY = 0.0f;
  bool dragging = false;
  float lastTouchY = 0.0f;

  constexpr float kOverscrollResistance = 0.45f;
  constexpr float kMomentumFriction = 4.5f;
  constexpr float kSpringSpeed = 14.0f;
  constexpr float kVelocitySmoothing = 0.35f;
  constexpr float kMaxFlingVelocity = 6000.0f;
  constexpr float kVelocityStopThreshold = 4.0f;

  // ---------------------------------------------------------
  // Header (title) show/hide state
  // ---------------------------------------------------------

  float titleVisible = 1.0f;
  float titleTarget = 1.0f;

  constexpr float kTitleY = 30.0f;
  constexpr float kTitleSlideDistance = 60.0f;
  constexpr float kTitleAnimSpeed = 10.0f;
  constexpr float kTitleScrollThreshold = 20.0f;

  constexpr float kSearchBoxHeight = 60.0f;
  constexpr float kSearchBoxMargin = 20.0f;
  constexpr float kSearchBoxYExpanded = 110.0f;
  constexpr float kSearchBoxYCollapsed = 20.0f;

  // ---------------------------------------------------------
  // Main loop
  // ---------------------------------------------------------

  bool initDraw = true;

  while (!WindowShouldClose()) {
    const float width = static_cast<float>(GetScreenWidth());
    const float height = static_cast<float>(GetScreenHeight());

    constexpr float listX = 20.0f;

    const float searchBoxY =
        kSearchBoxYCollapsed +
        (kSearchBoxYExpanded - kSearchBoxYCollapsed) * titleVisible;

    const float listY = searchBoxY + kSearchBoxHeight + kSearchBoxMargin;
    const float listWidth = width - 40.0f;
    const float listHeight = height - listY - 20.0f;

    const float prevScrollY = scrollY;
    const float dt = GetFrameTime();

    // -----------------------------------------------------
    // Draw
    // -----------------------------------------------------

    BeginDrawing();

    // Detect when the app comes back from background / lock screen
    static bool wasMinimized = false;

    if (IsWindowMinimized() || !IsWindowFocused()) {
      wasMinimized = true;
    } else if (wasMinimized) {
      // App just regained focus – force a complete redraw
      wasMinimized = false;

      // Optional but often helps:
      // Clear the whole screen once
      ClearBackground(RAYWHITE);
    }

    if (initDraw) {
      ClearBackground(RAYWHITE);
      initDraw = false;
    }

    // Title
    if (titleVisible > 0.01f) {
      const float titleY =
          kTitleY - (1.0f - titleVisible) * kTitleSlideDistance;

      DrawTextEx(font, "Application", Vector2{20.0f, titleY}, textSize, 0.0f,
                 Fade(DARKGRAY, titleVisible));
    }

    // -----------------------------------------------------
    // Search box
    // -----------------------------------------------------

    const Rectangle searchBoxRect{listX, searchBoxY, listWidth,
                                  kSearchBoxHeight};

// -----------------------------------------------------
// Inject soft-keyboard characters into the search buffer
// -----------------------------------------------------
#if defined(PLATFORM_ANDROID)
    {
      std::lock_guard<std::mutex> lock(gCharMutex);
      while (!gCharQueue.empty()) {
        int ch = gCharQueue.front();
        gCharQueue.pop();

        if (ch == 8) // Backspace
        {
          size_t len = strlen(searchBuffer);
          if (len > 0)
            searchBuffer[len - 1] = '\0';
        } else if (ch >= 32 && ch < 127) // printable ASCII
        {
          size_t len = strlen(searchBuffer);
          if (len + 1 < kSearchBufferSize) {
            searchBuffer[len] = static_cast<char>(ch);
            searchBuffer[len + 1] = '\0';
          }
        }
        // You can add more handling for other keys if needed
      }
    }
#endif

    if (GuiTextBox(searchBoxRect, searchBuffer, kSearchBufferSize,
                   searchEditMode)) {
      searchEditMode = !searchEditMode;
    }

    // Show / hide soft keyboard only when edit mode changes
    if (searchEditMode != prevSearchEditMode) {
      AndroidShowSoftKeyboard(searchEditMode);
      prevSearchEditMode = searchEditMode;
    }

    // -----------------------------------------------------
    // Filter items (case-insensitive substring)
    // -----------------------------------------------------

    std::string query(searchBuffer);
    std::transform(
        query.begin(), query.end(), query.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::vector<int> filtered;
    filtered.reserve(totalItemCount);

    for (int i = 0; i < totalItemCount; ++i) {
      if (query.empty()) {
        filtered.push_back(i);
        continue;
      }

      std::string lowerName = items[i];
      std::transform(
          lowerName.begin(), lowerName.end(), lowerName.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

      if (lowerName.find(query) != std::string::npos)
        filtered.push_back(i);
    }

    const int displayedCount = static_cast<int>(filtered.size());
    const float contentHeight = static_cast<float>(displayedCount) * itemHeight;
    const float maxScroll =
        contentHeight > listHeight ? contentHeight - listHeight : 0.0f;

    // -----------------------------------------------------
    // Touch scrolling + hide keyboard on outside tap
    // -----------------------------------------------------

    if (GetTouchPointCount() > 0) {
      const Vector2 touch = GetTouchPosition(0);

      // Hide keyboard when tapping outside the search box
      if (searchEditMode && !CheckCollisionPointRec(touch, searchBoxRect)) {
        searchEditMode = false;
        prevSearchEditMode = false;
        AndroidShowSoftKeyboard(false);
      }

      if (!dragging) {
        if (touch.x >= listX && touch.x <= listX + listWidth &&
            touch.y >= listY && touch.y <= listY + listHeight) {
          dragging = true;
          lastTouchY = touch.y;
          velocityY = 0.0f;
        }
      }

      if (dragging) {
        const float delta = lastTouchY - touch.y;
        float newScrollY = scrollY + delta;

        if (newScrollY < 0.0f) {
          newScrollY = scrollY + delta * kOverscrollResistance;
          if (newScrollY > 0.0f)
            newScrollY = 0.0f;
        } else if (newScrollY > maxScroll) {
          newScrollY = scrollY + delta * kOverscrollResistance;
          if (newScrollY < maxScroll)
            newScrollY = maxScroll;
        }

        scrollY = newScrollY;

        if (dt > 0.0f) {
          const float instantVelocity = delta / dt;
          velocityY += (instantVelocity - velocityY) * kVelocitySmoothing;
          velocityY =
              std::clamp(velocityY, -kMaxFlingVelocity, kMaxFlingVelocity);
        }

        lastTouchY = touch.y;
      }
    } else {
      dragging = false;
    }

    if (!dragging) {
      if (scrollY < 0.0f || scrollY > maxScroll) {
        const float target = scrollY < 0.0f ? 0.0f : maxScroll;
        scrollY += (target - scrollY) * (1.0f - std::exp(-kSpringSpeed * dt));
        if (std::fabs(scrollY - target) < 0.5f)
          scrollY = target;
        velocityY = 0.0f;
      } else if (std::fabs(velocityY) > kVelocityStopThreshold) {
        scrollY += velocityY * dt;
        velocityY *= std::exp(-kMomentumFriction * dt);
      } else {
        velocityY = 0.0f;
      }
    }

    // -----------------------------------------------------
    // Header show/hide
    // -----------------------------------------------------

    if (dt > 0.0f) {
      const float scrollSpeed = (scrollY - prevScrollY) / dt;

      if (maxScroll <= 0.0f || scrollY <= 1.0f)
        titleTarget = 1.0f;
      else if (scrollSpeed > kTitleScrollThreshold)
        titleTarget = 0.0f;
      else if (scrollSpeed < -kTitleScrollThreshold)
        titleTarget = 1.0f;
    }

    titleVisible +=
        (titleTarget - titleVisible) * (1.0f - std::exp(-kTitleAnimSpeed * dt));
    titleVisible = std::clamp(titleVisible, 0.0f, 1.0f);

    // -----------------------------------------------------
    // List
    // -----------------------------------------------------

    BeginScissorMode(static_cast<int>(listX), static_cast<int>(listY),
                     static_cast<int>(listWidth), static_cast<int>(listHeight));

    if (displayedCount == 0) {
      DrawTextEx(font, "No matches found",
                 Vector2{listX + 20.0f, listY + 20.0f}, textSize * 0.6f, 0.0f,
                 GRAY);
    } else {
      const int firstVisible = std::clamp(
          static_cast<int>(scrollY / itemHeight), 0, displayedCount - 1);

      const int lastVisible =
          std::clamp(static_cast<int>((scrollY + listHeight) / itemHeight) + 1,
                     0, displayedCount - 1);

      for (int row = firstVisible; row <= lastVisible; ++row) {
        const int itemIndex = filtered[row];
        const float y = listY + static_cast<float>(row) * itemHeight - scrollY;

        if ((row & 1) == 0) {
          DrawRectangle(static_cast<int>(listX), static_cast<int>(y),
                        static_cast<int>(listWidth),
                        static_cast<int>(itemHeight), LIGHTGRAY);
        }

        DrawTextEx(font, items[itemIndex].c_str(),
                   Vector2{listX + 20.0f, y + 15.0f}, textSize, 0.0f, BLACK);
      }
    }

    EndScissorMode();
    EndDrawing();
  }

  UnloadFont(font);
  CloseWindow();
  return EXIT_SUCCESS;
}

// Mobile / fullscreen raygui example
//
// Put raygui_mobile.h next to this source file.
//
// Build example:
//   g++ main.cpp -o mobile_app -lraylib -lm

#include "raylib.h"

#define RAYGUI_USE_TOUCH 1
#define RAYGUI_TOUCH_PADDING 10

#define RAYGUI_IMPLEMENTATION
#include "raygui_mobile.h"

#include <math.h>

struct MobileLayout
{
    float margin;
    float gap;
    float headerHeight;
    float footerHeight;
    float buttonHeight;
    float controlHeight;
    float contentWidth;
};

static MobileLayout GetMobileLayout()
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();
    const float shortest = fminf(width, height);

    MobileLayout l = {};

    // Scale UI while keeping controls comfortably touchable.
    const float scale = fmaxf(1.0f, shortest / 390.0f);

    l.margin = 16.0f * scale;
    l.gap = 12.0f * scale;
    l.headerHeight = 68.0f * scale;
    l.footerHeight = 76.0f * scale;
    l.buttonHeight = 58.0f * scale;
    l.controlHeight = 54.0f * scale;
    l.contentWidth = width - l.margin * 2.0f;

    return l;
}

static void DrawHeader(const MobileLayout& l)
{
    const float width = (float)GetScreenWidth();

    GuiPanel(
        (Rectangle){ 0, 0, width, l.headerHeight },
        NULL
    );

    GuiLabel(
        (Rectangle){
            l.margin,
            12,
            width - l.margin * 2.0f,
            28
        },
        "Mobile Dashboard"
    );

    GuiLabel(
        (Rectangle){
            l.margin,
            39,
            width - l.margin * 2.0f,
            18
        },
        "Touch-optimized raygui"
    );
}

static void DrawBottomBar(const MobileLayout& l, int* activeTab)
{
    const float width = (float)GetScreenWidth();
    const float height = (float)GetScreenHeight();

    Rectangle bar = {
        0,
        height - l.footerHeight,
        width,
        l.footerHeight
    };

    GuiPanel(bar, NULL);

    const float itemWidth = width / 3.0f;
    const float buttonHeight = l.footerHeight - 12.0f;

    GuiToggle(
        (Rectangle){
            0,
            bar.y + 6,
            itemWidth,
            buttonHeight
        },
        "Home",
        activeTab
    );

    GuiToggle(
        (Rectangle){
            itemWidth,
            bar.y + 6,
            itemWidth,
            buttonHeight
        },
        "Settings",
        activeTab
    );

    GuiToggle(
        (Rectangle){
            itemWidth * 2.0f,
            bar.y + 6,
            itemWidth,
            buttonHeight
        },
        "About",
        activeTab
    );
}

int main()
{
    // ---------------------------------------------------------------------
    // Fullscreen / mobile configuration
    // ---------------------------------------------------------------------

    SetConfigFlags(
        FLAG_FULLSCREEN_MODE |
        FLAG_VSYNC_HINT |
        FLAG_WINDOW_HIGHDPI
    );

    // raylib will use the display dimensions in fullscreen mode.
    InitWindow(1280, 720, "raygui Mobile");

    SetTargetFPS(60);

    // Enable the tap gesture used by the enhanced touch layer.
    SetGesturesEnabled(GESTURE_TAP);

    bool running = true;

    bool notifications = true;
    bool vibration = true;
    bool darkMode = true;

    float volume = 0.75f;
    int quality = 1;

    int activeTab = 0;

    // Persistent scroll position for the Settings page.
    Vector2 settingsScroll = { 0, 0 };

    while (!WindowShouldClose() && running)
    {
        const int screenWidth = GetScreenWidth();
        const int screenHeight = GetScreenHeight();

        const float width = (float)screenWidth;
        const float height = (float)screenHeight;

        const MobileLayout layout = GetMobileLayout();

        BeginDrawing();

        ClearBackground(
            GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR))
        );

        // -----------------------------------------------------------------
        // Header
        // -----------------------------------------------------------------

        DrawHeader(layout);

        // -----------------------------------------------------------------
        // Main content area
        // -----------------------------------------------------------------

        const float contentTop = layout.headerHeight + layout.gap;

        const float contentBottom =
            height - layout.footerHeight - layout.gap;

        const float contentHeight =
            contentBottom - contentTop;

        if (activeTab == 0)
        {
            // -------------------------------------------------------------
            // HOME
            // -------------------------------------------------------------

            GuiLabel(
                (Rectangle){
                    layout.margin,
                    contentTop,
                    layout.contentWidth,
                    32
                },
                "Welcome"
            );

            const float cardY = contentTop + 44.0f;

            GuiPanel(
                (Rectangle){
                    layout.margin,
                    cardY,
                    layout.contentWidth,
                    120
                },
                NULL
            );

            GuiLabel(
                (Rectangle){
                    layout.margin + 16,
                    cardY + 16,
                    layout.contentWidth - 32,
                    24
                },
                "Touch interface ready"
            );

            GuiLabel(
                (Rectangle){
                    layout.margin + 16,
                    cardY + 48,
                    layout.contentWidth - 32,
                    48
                },
                "Buttons, toggles, sliders and scrolling are\n"
                "optimized for finger interaction."
            );

            if (GuiButton(
                    (Rectangle){
                        layout.margin,
                        cardY + 140,
                        layout.contentWidth,
                        layout.buttonHeight
                    },
                    "Start"))
            {
                TraceLog(LOG_INFO, "Start pressed");
            }
        }
        else if (activeTab == 1)
        {
            // -------------------------------------------------------------
            // SETTINGS
            // -------------------------------------------------------------

            // Content is deliberately taller than the viewport so it can
            // be finger-scrolled on phones.
            const float contentWidth = layout.contentWidth;
            const float contentHeightTotal =
                640.0f * fmaxf(
                    1.0f,
                    fminf(width, height) / 390.0f
                );

            Rectangle view = {
                layout.margin,
                contentTop,
                contentWidth,
                contentHeight
            };

            Rectangle content = {
                0,
                0,
                contentWidth,
                contentHeightTotal
            };

            GuiScrollPanel(
                (Rectangle){
                    layout.margin,
                    contentTop,
                    contentWidth,
                    contentHeight
                },
                NULL,
                content,
                &settingsScroll,
                &view
            );

            // Convert scroll-panel coordinates into screen coordinates.
            const float x = layout.margin + 12.0f;
            const float w = contentWidth - 24.0f;
            const float y0 = contentTop + settingsScroll.y + 16.0f;

            GuiLabel(
                (Rectangle){ x, y0, w, 30 },
                "Preferences"
            );

            // Notifications
            GuiCheckBox(
                (Rectangle){
                    x,
                    y0 + 48,
                    36,
                    36
                },
                "Notifications",
                &notifications
            );

            // Vibration
            GuiCheckBox(
                (Rectangle){
                    x,
                    y0 + 104,
                    36,
                    36
                },
                "Vibration",
                &vibration
            );

            // Dark mode
            GuiCheckBox(
                (Rectangle){
                    x,
                    y0 + 160,
                    36,
                    36
                },
                "Dark mode",
                &darkMode
            );

            GuiLabel(
                (Rectangle){
                    x,
                    y0 + 224,
                    w,
                    24
                },
                "Volume"
            );

            GuiSlider(
                (Rectangle){
                    x,
                    y0 + 258,
                    w,
                    54
                },
                "0",
                "100",
                &volume,
                0.0f,
                1.0f
            );

            GuiLabel(
                (Rectangle){
                    x,
                    y0 + 328,
                    w,
                    24
                },
                "Graphics quality"
            );

            GuiToggleGroup(
                (Rectangle){
                    x,
                    y0 + 364,
                    w,
                    layout.buttonHeight
                },
                "Low;Medium;High",
                &quality
            );

            if (GuiButton(
                    (Rectangle){
                        x,
                        y0 + 440,
                        w,
                        layout.buttonHeight
                    },
                    "Save Settings"))
            {
                TraceLog(
                    LOG_INFO,
                    "Settings saved: notifications=%d vibration=%d dark=%d volume=%.2f quality=%d",
                    notifications,
                    vibration,
                    darkMode,
                    volume,
                    quality
                );
            }
        }
        else
        {
            // -------------------------------------------------------------
            // ABOUT
            // -------------------------------------------------------------

            GuiLabel(
                (Rectangle){
                    layout.margin,
                    contentTop,
                    layout.contentWidth,
                    32
                },
                "About"
            );

            GuiPanel(
                (Rectangle){
                    layout.margin,
                    contentTop + 48,
                    layout.contentWidth,
                    170
                },
                NULL
            );

            GuiLabel(
                (Rectangle){
                    layout.margin + 16,
                    contentTop + 66,
                    layout.contentWidth - 32,
                    28
                },
                "raygui Mobile Example"
            );

            GuiLabel(
                (Rectangle){
                    layout.margin + 16,
                    contentTop + 102,
                    layout.contentWidth - 32,
                    70
                },
                "Fullscreen touch interface using\n"
                "raylib + the enhanced raygui header."
            );
        }

        // -----------------------------------------------------------------
        // Bottom navigation
        // -----------------------------------------------------------------

        DrawBottomBar(layout, &activeTab);

        // -----------------------------------------------------------------
        // Exit handling
        // -----------------------------------------------------------------

        // Keep desktop keyboard support while the primary interaction is
        // touch-oriented.
        if (IsKeyPressed(KEY_ESCAPE))
        {
            running = false;
        }

        EndDrawing();
    }

    CloseWindow();

    return 0;
}

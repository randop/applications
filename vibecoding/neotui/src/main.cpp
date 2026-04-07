#include <lua.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <cstring>
#include <chrono>
#include <ctime>
#include <vector>
#include <sstream>

int main() {
    // Initialize Lua state like Neovim does
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    // Tokyo Night Moon (dark) colors
    auto bg_color_dark = ftxui::Color(34, 36, 54);     // #222436
    auto fg_color_dark = ftxui::Color(200, 211, 245);  // #c8d3f5
    auto accent_color_dark = ftxui::Color(130, 170, 255); // #82aaff
    auto border_color_dark = ftxui::Color(59, 66, 97);  // #3b4261 (fg_gutter)
    auto status_bg_dark = ftxui::Color(47, 51, 77);     // #2f334d (bg_highlight)

    // Tokyo Night Day (light) colors
    auto bg_color_light = ftxui::Color(233, 233, 237); // #e9e9ed
    auto fg_color_light = ftxui::Color(55, 96, 191);   // #3760bf
    auto accent_color_light = ftxui::Color(46, 125, 225); // #2e7de1
    auto border_color_light = ftxui::Color(168, 174, 203); // #a8aecb
    auto status_bg_light = ftxui::Color(200, 200, 210); // light status bg

    // Load config/init.lua
    bool dark_theme = true; // Default
    bool config_loaded = false;
    if (luaL_dofile(L, "config/init.lua") == LUA_OK) {
        lua_getglobal(L, "ui");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "theme");
            if (lua_isstring(L, -1)) {
                const char* theme_str = lua_tostring(L, -1);
                if (strcmp(theme_str, "light") == 0) {
                    dark_theme = false;
                } else if (strcmp(theme_str, "dark") == 0) {
                    dark_theme = true;
                }
            }
            lua_pop(L, 1); // pop theme
        }
        lua_pop(L, 1); // pop ui
        config_loaded = true;
    }

    // Initialize FTXUI screen
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    std::string code;
    std::string output;

    // Input component for Lua code
    auto input_component = ftxui::Input(&code, "Enter Lua code here...");

    // Button to run the code
    auto run_button = ftxui::Button("Run Lua Code", [&L, &code, &output]() {
        if (luaL_dostring(L, code.c_str()) == LUA_OK) {
            // If successful, check for return value
            if (lua_gettop(L) > 0) {
                output = lua_tostring(L, -1);
                lua_pop(L, 1);
            } else {
                output = "Code executed successfully.";
            }
        } else {
            // If error, get error message
            output = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
    });

    // Function to split string by delimiter
    auto split = [](const std::string& s, char delim) {
        std::vector<std::string> result;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, delim)) {
            result.push_back(item);
        }
        return result;
    };

    // Buffer renderer (code with line numbers)
    auto buffer_renderer = ftxui::Renderer([&]() {
        bool is_dark = dark_theme;
        auto bg = is_dark ? bg_color_dark : bg_color_light;
        auto fg = is_dark ? fg_color_dark : fg_color_light;
        auto gutter_fg = is_dark ? border_color_dark : border_color_light;

        std::vector<std::string> lines = split(code, '\n');
        if (lines.empty()) lines.push_back(""); // at least one line

        ftxui::Elements buffer_lines;
        for (size_t i = 0; i < lines.size(); ++i) {
            std::string line_num = std::to_string(i + 1);
            buffer_lines.push_back(
                ftxui::hbox({
                    ftxui::text(line_num) | ftxui::color(gutter_fg) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 4),
                    ftxui::text(" "),
                    ftxui::text(lines[i]) | ftxui::color(fg)
                })
            );
        }

        return ftxui::vbox(buffer_lines) | ftxui::bgcolor(bg) | ftxui::yflex;
    });

    // Output renderer
    auto output_renderer = ftxui::Renderer([&]() {
        bool is_dark = dark_theme;
        auto bg = is_dark ? bg_color_dark : bg_color_light;
        auto fg = is_dark ? fg_color_dark : fg_color_light;
        auto border_fg = is_dark ? border_color_dark : border_color_light;
        return ftxui::text(output) | ftxui::bgcolor(bg) | ftxui::color(fg) | ftxui::borderStyled(ftxui::BorderStyle::ROUNDED, border_fg);
    });

    // Layout: vertical container with input, button, output
    auto container = ftxui::Container::Vertical({
        input_component,
        run_button,
        output_renderer
    });

    // Main renderer with LazyVim-like layout
    auto renderer = ftxui::Renderer(container, [&]() {
        bool is_dark = dark_theme;
        auto bg = is_dark ? bg_color_dark : bg_color_light;
        auto fg = is_dark ? fg_color_dark : fg_color_light;
        auto accent = is_dark ? accent_color_dark : accent_color_light;
        auto border_fg = is_dark ? border_color_dark : border_color_light;
        auto status_bg = is_dark ? status_bg_dark : status_bg_light;

        // Get current time
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::string time_str = std::ctime(&t);
        time_str.erase(time_str.find_last_not_of(" \n\r\t") + 1); // trim trailing whitespace

        // Status line (LazyVim style)
        std::string theme_name = dark_theme ? "Moon" : "Day";
        std::string config_status = config_loaded ? "OK" : "Failed";
        std::string status = " NORMAL  lua  NeoTUI  Theme: " + theme_name + "  Config: " + config_status + "  " + time_str;

        return ftxui::vbox({
            ftxui::text("NeoTUI") | ftxui::bold | ftxui::color(accent) | ftxui::center,
            ftxui::separator() | ftxui::color(border_fg),
            buffer_renderer->Render() | ftxui::yflex,
            ftxui::separator() | ftxui::color(border_fg),
            ftxui::hbox({
                ftxui::text(":") | ftxui::color(accent),
                input_component->Render() | ftxui::bgcolor(bg) | ftxui::color(fg) | ftxui::xflex,
                run_button->Render() | ftxui::bgcolor(bg) | ftxui::color(fg)
            }),
            output_renderer->Render(),
            ftxui::separator() | ftxui::color(border_fg),
            ftxui::text(status) | ftxui::color(fg) | ftxui::bgcolor(status_bg)
        }) | ftxui::bgcolor(bg) | ftxui::color(fg) | ftxui::yflex;
    });

    // Run the loop
    screen.Loop(renderer);

    // Clean up Lua state
    lua_close(L);
    return 0;
}
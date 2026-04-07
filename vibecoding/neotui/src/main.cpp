#include <lua.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
#include <string>
#include <cstring>
#include <chrono>
#include <ctime>
#include <vector>
#include <sstream>
#include <filesystem>
#include <map>

struct ThemeColors {
    ftxui::Color background;
    ftxui::Color foreground;
    ftxui::Color accent;
    ftxui::Color border;
    ftxui::Color statusbar;
    ftxui::Color fg_gutter;
    ftxui::Color bg_highlight;
    ftxui::Color comment;
    ftxui::Color green;
    ftxui::Color yellow;
    ftxui::Color red;
    ftxui::Color magenta;
    ftxui::Color purple;
    ftxui::Color cyan;
    ftxui::Color orange;
    std::string name;
};

std::map<std::string, ThemeColors> loadThemes() {
    return {
        {"dark", {
            ftxui::Color::RGB(34, 36, 54),     // bg - #222436
            ftxui::Color::RGB(200, 211, 245), // fg - #c8d3f5
            ftxui::Color::RGB(130, 170, 255), // blue - #82aaff
            ftxui::Color::RGB(59, 66, 97),    // border - #3b4261
            ftxui::Color::RGB(47, 51, 77),    // statusbar - #2f334d
            ftxui::Color::RGB(59, 66, 97),    // fg_gutter - #3b4261
            ftxui::Color::RGB(47, 51, 77),    // bg_highlight - #2f334d
            ftxui::Color::RGB(99, 109, 166),  // comment - #636da6
            ftxui::Color::RGB(195, 232, 141), // green - #c3e88d
            ftxui::Color::RGB(255, 199, 119), // yellow - #ffc777
            ftxui::Color::RGB(255, 117, 127), // red - #ff757f
            ftxui::Color::RGB(192, 153, 255), // magenta - #c099ff
            ftxui::Color::RGB(252, 167, 234), // purple - #fca7ea
            ftxui::Color::RGB(134, 225, 252), // cyan - #86e1fc
            ftxui::Color::RGB(255, 150, 108), // orange - #ff966c
            "Moon"
        }},
        {"light", {
            ftxui::Color::RGB(233, 233, 237), // bg - #e9e9ed
            ftxui::Color::RGB(55, 96, 191),   // fg - #3760bf
            ftxui::Color::RGB(46, 125, 225),   // blue - #2e7de1
            ftxui::Color::RGB(168, 174, 203), // border - #a8aecb
            ftxui::Color::RGB(200, 200, 210), // statusbar
            ftxui::Color::RGB(168, 174, 203), // fg_gutter
            ftxui::Color::RGB(200, 200, 210), // bg_highlight
            ftxui::Color::RGB(128, 132, 163), // comment
            ftxui::Color::RGB(121, 192, 105), // green
            ftxui::Color::RGB(255, 183, 78),  // yellow
            ftxui::Color::RGB(213, 73, 75),   // red
            ftxui::Color::RGB(175, 97, 255),  // magenta
            ftxui::Color::RGB(197, 108, 240), // purple
            ftxui::Color::RGB(56, 175, 183),  // cyan
            ftxui::Color::RGB(255, 130, 90),  // orange
            "Day"
        }}
    };
}

int main() {
    // Initialize Lua state like Neovim does
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    lua_newtable(L);
    lua_setglobal(L, "ui");

    auto themes = loadThemes();
    std::string selected_theme = "dark";
    ThemeColors theme = themes[selected_theme];
    
    bool config_loaded = false;
    
    std::string config_path;
    std::filesystem::path cwd = std::filesystem::current_path();
    if (std::filesystem::exists(cwd / "config/init.lua")) {
        config_path = (cwd / "config/init.lua").string();
    } else if (std::filesystem::exists(cwd / "build/config/init.lua")) {
        config_path = (cwd / "build/config/init.lua").string();
    } else {
        config_path = (cwd / "config/init.lua").string();
    }
    
    int load_result = luaL_loadfile(L, config_path.c_str());
    if (load_result == LUA_OK) {
        load_result = lua_pcall(L, 0, LUA_MULTRET, 0);
    }
    if (load_result == LUA_OK) {
        lua_getglobal(L, "ui");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "theme");
            if (lua_isstring(L, -1)) {
                const char* theme_str = lua_tostring(L, -1);
                if (themes.count(theme_str)) {
                    selected_theme = theme_str;
                    theme = themes[selected_theme];
                }
            }
            lua_pop(L, 1); // pop theme
        }
        lua_pop(L, 1); // pop ui
        config_loaded = true;
    } else {
        const char* err = lua_tostring(L, -1);
        fprintf(stderr, "Config load error: %s\n", err ? err : "unknown");
        lua_pop(L, 1);
    }

    // Initialize FTXUI with TrueColor support before creating screen
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    std::string code;
    std::string output;

    auto input_option = ftxui::InputOption::Default();
    input_option.transform = [&theme](ftxui::InputState state) {
        auto bg = theme.background;
        auto fg = theme.foreground;
        auto focus_fg = theme.yellow;
        
        auto fg_color = state.focused ? focus_fg : fg;
        auto border_fg = state.focused ? theme.yellow : theme.border;
        
        auto element = state.element | ftxui::bgcolor(bg) | ftxui::color(fg_color);
        
        if (state.focused) {
            element = element | ftxui::borderStyled(ftxui::BorderStyle::ROUNDED, border_fg);
        }
        
        return element;
    };
    
    // Input component for Lua code
    auto input_component = ftxui::Input(&code, "Enter Lua code here...", input_option);

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
        auto bg = theme.background;
        auto fg = theme.foreground;
        auto gutter_fg = theme.fg_gutter;

        std::vector<std::string> lines = split(code, '\n');
        if (lines.empty()) lines.push_back("");

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
        auto bg = theme.background;
        auto fg = theme.foreground;
        auto border_fg = theme.border;
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
        auto bg = theme.background;
        auto fg = theme.foreground;
        auto accent = theme.accent;
        auto border_fg = theme.border;
        auto status_bg = theme.statusbar;

        // Get current time
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::string time_str = std::ctime(&t);
        time_str.erase(time_str.find_last_not_of(" \n\r\t") + 1);

        // Status line (LazyVim style)
        std::string config_status = config_loaded ? "OK" : "Failed";
        std::string left_status = " NORMAL  lua  NeoTUI  Theme: " + theme.name + "  Config: " + config_status;

        return ftxui::vbox({
            buffer_renderer->Render() | ftxui::yflex,
            ftxui::separator() | ftxui::color(border_fg),
            ftxui::hbox({
                ftxui::text(":") | ftxui::color(accent),
                input_component->Render() | ftxui::bgcolor(bg) | ftxui::color(fg) | ftxui::xflex,
                run_button->Render() | ftxui::bgcolor(bg) | ftxui::color(fg)
            }),
            output_renderer->Render(),
            ftxui::separator() | ftxui::color(border_fg),
            ftxui::hbox({
                ftxui::text(left_status) | ftxui::color(fg) | ftxui::bgcolor(status_bg),
                ftxui::filler() | ftxui::bgcolor(status_bg),
                ftxui::text(time_str) | ftxui::color(fg) | ftxui::bgcolor(status_bg)
            }) | ftxui::bgcolor(status_bg)
        }) | ftxui::bgcolor(bg) | ftxui::color(fg) | ftxui::yflex;
    });

    // Run the loop
    screen.Loop(renderer);

    // Clean up Lua state
    lua_close(L);
    return 0;
}
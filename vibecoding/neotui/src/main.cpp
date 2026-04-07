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
#include <filesystem>
#include <map>

struct ThemeColors {
    ftxui::Color background;
    ftxui::Color foreground;
    ftxui::Color accent;
    ftxui::Color border;
    ftxui::Color statusbar;
    std::string name;
};

std::map<std::string, ThemeColors> loadThemes() {
    return {
        {"dark", {
            ftxui::Color(34, 36, 54),    // background
            ftxui::Color(200, 211, 245), // foreground
            ftxui::Color(130, 170, 255), // accent
            ftxui::Color(59, 66, 97),    // border
            ftxui::Color(47, 51, 77),    // statusbar
            "Moon"
        }},
        {"light", {
            ftxui::Color(233, 233, 237), // background
            ftxui::Color(55, 96, 191),   // foreground
            ftxui::Color(46, 125, 225),  // accent
            ftxui::Color(168, 174, 203), // border
            ftxui::Color(200, 200, 210), // statusbar
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

    // Load config/init.lua
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
        auto bg = theme.background;
        auto fg = theme.foreground;
        auto gutter_fg = theme.border;

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
        std::string status = " NORMAL  lua  NeoTUI  Theme: " + theme.name + "  Config: " + config_status + "  " + time_str;

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
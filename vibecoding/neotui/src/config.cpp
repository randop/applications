#include "config.hpp"
#include <lua.hpp>
#include <iostream>

namespace neotui {
namespace config {

std::map<std::string, ThemeColors> loadThemes() {
    return {
        {"dark", {
            ftxui::Color::RGB(34, 36, 54),
            ftxui::Color::RGB(200, 211, 245),
            ftxui::Color::RGB(130, 170, 255),
            ftxui::Color::RGB(59, 66, 97),
            ftxui::Color::RGB(47, 51, 77),
            ftxui::Color::RGB(59, 66, 97),
            ftxui::Color::RGB(47, 51, 77),
            ftxui::Color::RGB(99, 109, 166),
            ftxui::Color::RGB(195, 232, 141),
            ftxui::Color::RGB(255, 199, 119),
            ftxui::Color::RGB(255, 117, 127),
            ftxui::Color::RGB(192, 153, 255),
            ftxui::Color::RGB(252, 167, 234),
            ftxui::Color::RGB(134, 225, 252),
            ftxui::Color::RGB(255, 150, 108),
            "Moon"
        }},
        {"light", {
            ftxui::Color::RGB(233, 233, 237),
            ftxui::Color::RGB(55, 96, 191),
            ftxui::Color::RGB(46, 125, 225),
            ftxui::Color::RGB(168, 174, 203),
            ftxui::Color::RGB(200, 200, 210),
            ftxui::Color::RGB(168, 174, 203),
            ftxui::Color::RGB(200, 200, 210),
            ftxui::Color::RGB(128, 132, 163),
            ftxui::Color::RGB(121, 192, 105),
            ftxui::Color::RGB(255, 183, 78),
            ftxui::Color::RGB(213, 73, 75),
            ftxui::Color::RGB(175, 97, 255),
            ftxui::Color::RGB(197, 108, 240),
            ftxui::Color::RGB(56, 175, 183),
            ftxui::Color::RGB(255, 130, 90),
            "Day"
        }}
    };
}

Config::Config() : theme_name("dark"), themes(loadThemes()) {}

Config::~Config() = default;

bool Config::load(const std::string& config_path, lua_State* L) {
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
                    theme_name = theme_str;
                }
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return true;
    } else {
        std::cerr << "Config load error: " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
        return false;
    }
}

std::string Config::getTheme() const {
    return theme_name;
}

std::map<std::string, ThemeColors> Config::getThemes() const {
    return themes;
}

}
}

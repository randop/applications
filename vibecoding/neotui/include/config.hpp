#pragma once

#ifndef NEO_CONFIG_H
#define NEO_CONFIG_H

#include <string>
#include <map>
#include <lua.hpp>

#include "tui.hpp"

namespace neotui {
namespace config {

class Config {
public:
    Config();
    ~Config();

    bool load(const std::string& config_path, lua_State* L);
    std::string getTheme() const;
    std::map<std::string, ThemeColors> getThemes() const;

private:
    std::string theme_name;
    std::map<std::string, ThemeColors> themes;
};

} // namespace config
} // namespace neotui

#endif // NEO_CONFIG_H

#pragma once

#ifndef NEO_PLUGINS_H
#define NEO_PLUGINS_H

#include <string>
#include <lua.hpp>

namespace neotui {
namespace plugins {

bool loadInit(lua_State* L, const std::string& init_path);
bool loadAPI(lua_State* L, const std::string& api_path);
bool renderPanel(lua_State* L, const std::string& panel_path, std::string& output);
bool runProcess(lua_State* L, const std::string& process_path, std::string& output);

} // namespace plugins
} // namespace neotui

#endif // NEO_PLUGINS_H

#pragma once

#ifndef NEO_API_H
#define NEO_API_H

#include <string>
#include <lua.hpp>

namespace neotui {
namespace api {

void register_in_lua(lua_State* L);
void set_panel3_content(const std::string& content);
std::string get_panel3_content();

} // namespace api
} // namespace neotui

#endif // NEO_API_H
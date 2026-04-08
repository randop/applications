#include "api.hpp"
#include <string>

namespace neotui {
namespace api {

static std::string panel3_content;

static int l_set_panel3_content(lua_State* L) {
    const char* content = luaL_checkstring(L, 1);
    panel3_content = std::string(content);
    return 0;
}

static int l_get_panel3_content(lua_State* L) {
    lua_pushstring(L, panel3_content.c_str());
    return 1;
}

static const luaL_Reg api_lib[] = {
    { "set_panel3_content",    l_set_panel3_content   },
    { "get_panel3_content",    l_get_panel3_content   },
    { nullptr, nullptr }
};

void register_in_lua(lua_State* L) {
    luaL_newlib(L, api_lib);
    lua_setglobal(L, "api");
}

void set_panel3_content(const std::string& content) {
    panel3_content = content;
}

std::string get_panel3_content() {
    return panel3_content;
}

} // namespace api
} // namespace neotui
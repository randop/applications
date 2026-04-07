#include "plugins.hpp"
#include <iostream>

namespace neotui {
namespace plugins {

bool loadInit(lua_State* L, const std::string& init_path) {
    if (luaL_loadfile(L, init_path.c_str()) == LUA_OK) {
        if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
            std::cerr << "Plugins load error: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
            return false;
        }
        return true;
    }
    std::cerr << "Failed to load plugins: " << init_path << std::endl;
    return false;
}

bool runProcess(lua_State* L, const std::string& process_path, std::string& output) {
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_remove(L, -2);
    
    int errfunc = lua_gettop(L);
    
    if (luaL_loadfile(L, process_path.c_str()) == LUA_OK) {
        if (lua_pcall(L, 0, 1, errfunc) == LUA_OK) {
            output = lua_tostring(L, -1);
            lua_pop(L, 1);
        } else {
            std::cerr << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
            output.clear();
            return false;
        }
    } else {
        std::cerr << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
        output.clear();
        return false;
    }
    
    lua_settop(L, 0);
    return true;
}

}
}

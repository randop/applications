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

bool loadAPI(lua_State* L, const std::string& api_path) {
    if (luaL_loadfile(L, api_path.c_str()) == LUA_OK) {
        if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
            std::cerr << "API load error: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
            return false;
        }
        return true;
    }
    std::cerr << "Failed to load API: " << api_path << std::endl;
    return false;
}

bool renderPanel(lua_State* L, const std::string& panel_path, std::string& output) {
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_remove(L, -2);

    int errfunc = lua_gettop(L);

    if (luaL_loadfile(L, panel_path.c_str()) == LUA_OK) {
        if (lua_pcall(L, 0, 1, errfunc) == LUA_OK) {
            // The panel.lua file returned a table, now call panel.demo() on it
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "demo");
                if (lua_isfunction(L, -1)) {
                    lua_pushvalue(L, -2);  // Push the panel table as self
                    if (lua_pcall(L, 1, 1, errfunc) == LUA_OK) {
                        output = lua_tostring(L, -1);
                        lua_pop(L, 2);  // Pop result and panel table
                        lua_settop(L, 0);
                        return true;
                    } else {
                        std::cerr << "Panel demo error: " << lua_tostring(L, -1) << std::endl;
                        lua_pop(L, 2);
                    }
                } else {
                    std::cerr << "Panel demo is not a function" << std::endl;
                    lua_pop(L, 1);
                }
            } else {
                std::cerr << "Panel file did not return a table" << std::endl;
                lua_pop(L, 1);
            }
        } else {
            std::cerr << "Panel load error: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
        }
    } else {
        std::cerr << "Failed to load panel: " << panel_path << std::endl;
        lua_pop(L, 1);
    }

    output.clear();
    lua_settop(L, 0);
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

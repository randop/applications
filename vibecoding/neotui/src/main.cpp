#include <iostream>
#include <filesystem>
#include <cstdlib>
#include "tui.hpp"
#include "config.hpp"
#include "plugins.hpp"
#include "bridge.hpp"

int main() {
    setenv("COLORTERM", "truecolor", 1);
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);

    std::filesystem::path cwd = std::filesystem::current_path();
    
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    
    neotui::Bridge bridge;
    bridge.register_in_lua(L);
    
    lua_newtable(L);
    lua_setglobal(L, "ui");
    
    std::string plugins_path = (cwd / "plugins/init.lua").string();
    if (!std::filesystem::exists(plugins_path)) {
        plugins_path = (cwd / "build/plugins/init.lua").string();
    }
    
    std::string process_path = plugins_path;
    {
        size_t pos = process_path.rfind("init.lua");
        if (pos != std::string::npos) {
            process_path.replace(pos, 8, "process.lua");
        }
    }
    
    neotui::config::Config config;
    config.load((cwd / "config/init.lua").string(), L);
    
    neotui::TUI tui;
    tui.setTheme(config.getThemes()[config.getTheme()]);
    tui.setConfigStatus(true);
    tui.setBridge(&bridge);
    
    neotui::plugins::loadInit(L, plugins_path);
    
    bool workspace_loaded = false;
    std::string workspace_path = (cwd / "plugins/workspace.lua").string();
    if (!std::filesystem::exists(workspace_path)) {
        workspace_path = (cwd / "build/plugins/workspace.lua").string();
    }
    
    tui.setOnPanelChange([&L, &workspace_path, &workspace_loaded](int panel) {
        if (panel == 1 && !workspace_loaded) {
            neotui::plugins::loadInit(L, workspace_path);
            workspace_loaded = true;
        }
    });
    
    tui.setOnRun([&L, &tui]() {
        std::string code = tui.getCode();
        int top_before = lua_gettop(L);
        
        if (luaL_dostring(L, code.c_str()) == LUA_OK) {
            int top_after = lua_gettop(L);
            if (top_after > top_before) {
                const char* result = lua_tostring(L, -1);
                tui.setCodeOutput(result ? result : "nil");
                lua_pop(L, 1);
            } else {
                tui.setCodeOutput("Code executed successfully.");
            }
        } else {
            const char* error = lua_tostring(L, -1);
            tui.setCodeOutput(error ? error : "Unknown error");
            lua_pop(L, 1);
        }
        
        lua_settop(L, 0);
    });
    
    tui.setOnProcess([&L, &tui, &process_path]() {
        std::string output;
        neotui::plugins::runProcess(L, process_path, output);
        tui.setWorkspaceOutput(output);
    });
    
    tui.run();
    
    lua_close(L);
    return 0;
}

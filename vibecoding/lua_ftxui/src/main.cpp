// main.cpp — lua_ftxui entry point
//
// Flow:
//   1. Create lua_State
//   2. Create Store
//   3. Register the ftxui Lua API
//   4. Execute the Lua script (which populates the store and sets a root component)
//   5. Run the FTXUI ScreenInteractive event loop
//   6. Clean up

#include "lua_ftxui.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace {

// Locate the Lua script: prefer a path given on the command line, then look
// for "scripts/ui.lua" relative to the working directory.
std::string resolve_script(int argc, char** argv) {
    if (argc >= 2) return argv[1];
    // Common build-directory layout: binary lives in build/, scripts in build/scripts/
    for (auto&& candidate : { "scripts/ui.lua", "../scripts/ui.lua" }) {
        if (std::filesystem::exists(candidate)) return candidate;
    }
    return "scripts/ui.lua"; // let Lua report a clean error
}

} // namespace

int main(int argc, char** argv) {
    // ── Lua state ─────────────────────────────────────────────────────────────
    lua_State* L = luaL_newstate();
    if (!L) {
        std::cerr << "[fatal] failed to create Lua state\n";
        return EXIT_FAILURE;
    }
    luaL_openlibs(L);

    // ── Store ─────────────────────────────────────────────────────────────────
    lua_ftxui::Store store;

    // ── Register FTXUI API into Lua ───────────────────────────────────────────
    lua_ftxui::register_ftxui(L, store);

    // ── Execute Lua script ────────────────────────────────────────────────────
    std::string script = resolve_script(argc, argv);
    if (luaL_dofile(L, script.c_str()) != LUA_OK) {
        std::cerr << "[lua error] " << lua_tostring(L, -1) << "\n";
        lua_close(L);
        return EXIT_FAILURE;
    }

    // ── Validate that the script called ftxui.set_root() ─────────────────────
    if (!store.root) {
        std::cerr << "[error] Lua script did not call ftxui.set_root()\n";
        lua_close(L);
        return EXIT_FAILURE;
    }

    // ── FTXUI event loop ──────────────────────────────────────────────────────
    auto screen = ftxui::ScreenInteractive::Fullscreen();

    // Attach a quit mechanism: if Lua called ftxui.quit() during a callback,
    // we detect it on the next render tick via a CatchEvent wrapper.
    auto root_with_quit = ftxui::CatchEvent(store.root, [&](ftxui::Event) {
        if (store.quit_requested) {
            screen.ExitLoopClosure()();
        }
        return false; // let other handlers run
    });

    screen.Loop(root_with_quit);

    // ── Cleanup ───────────────────────────────────────────────────────────────
    lua_close(L);
    return EXIT_SUCCESS;
}

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

// C++ callback invoked by Lua timer
int cpp_callback(lua_State *L) {
  if (lua_gettop(L) != 1 || !lua_isstring(L, 1)) {
    return luaL_error(L,
                      "cpp_callback: Expected one string argument (timestamp)");
  }
  std::string_view timestamp(lua_tostring(L, 1),
                             static_cast<size_t>(lua_rawlen(L, 1)));
  std::cout << "Called from Lua timer with timestamp: " << timestamp
            << std::endl;
  return 0; // No return values to Lua
}

// Lua-exposed sleep function (in seconds, as double)
int lua_sleep(lua_State *L) {
  double seconds = luaL_checknumber(L, 1);
  if (seconds < 0) {
    luaL_error(L, "sleep: negative seconds not allowed");
  }
  std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
  return 0;
}

void run_lua_background() {
  lua_State *L = luaL_newstate();
  if (!L) {
    std::cerr << "Failed to create Lua state" << std::endl;
    return;
  }

  luaL_openlibs(L); // Load standard Lua libraries

  // Register C++ functions for Lua
  lua_register(L, "cpp_callback", cpp_callback);
  lua_register(L, "sleep", lua_sleep);

  // Load and execute the Lua script
  if (luaL_dofile(L, "app.lua") != LUA_OK) {
    std::cerr << "Lua error: " << lua_tostring(L, -1) << std::endl;
    lua_pop(L, 1); // Remove error message
  }

  lua_close(L);
  std::cout << "Lua background thread finished" << std::endl;
}

int main() {
  std::cout << "Starting main thread and spawning Lua background thread..."
            << std::endl;

  // Spawn Lua in background thread
  std::thread lua_thread(run_lua_background);

  // Main thread does non-blocking work (e.g., periodic logging for 10 seconds)
  auto start_time = std::chrono::steady_clock::now();
  while (std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                       start_time)
             .count() < 10.0) {
    std::cout << "Main thread working..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Wait for Lua thread to complete
  lua_thread.join();
  std::cout << "Main thread exiting" << std::endl;

  return EXIT_SUCCESS;
}

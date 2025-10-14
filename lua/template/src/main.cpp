#include <iostream>
#include <lua.hpp>
#include <string>

int main() {
  lua_State *L = luaL_newstate();
  if (!L) {
    std::cerr << "Failed to create Lua state" << std::endl;
    return 1;
  }

  luaL_openlibs(L);
  const char *lua_script = R"(
    print('Lua: Hello World!!!')
  )";

  // Load and run the script
  if (luaL_loadstring(L, lua_script) || lua_pcall(L, 0, LUA_MULTRET, 0)) {
    std::cerr << "Lua error: " << lua_tostring(L, -1) << std::endl;
    lua_pop(L, 1);
  }

  // Cleanup
  lua_close(L);
  std::cout << "Embedded Lua: OK" << std::endl;
  return 0;
}
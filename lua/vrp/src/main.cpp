#include <lua.hpp>
#include <iostream>
#include <string>
#include <fstream>

extern "C" int luaopen_socket_core(lua_State* L);
extern "C" int luaopen_mime_core(lua_State* L);
extern "C" int luaopen_ssl_core(lua_State* L);
extern "C" int luaopen_ssl_context(lua_State* L);
extern "C" int luaopen_ssl_x509(lua_State* L);
extern "C" int luaopen_ssl_config(lua_State* L);

// Function to load Lua file as string (relative to executable)
std::string load_lua_file(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open Lua file: " + filename);
  }
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// Helper to set module in package.loaded (module table at -1 before call)
void set_module(lua_State* L, const char* modname) {
  // Stack: ... module_table (-1)
  lua_getglobal(L, "package.loaded");  // ... module_table (-2), loaded (-1)
  lua_pushstring(L, modname);          // ... module_table (-3), loaded (-2), modname (-1)
  lua_pushvalue(L, -3);                // ... module_table (-3), loaded (-2), modname (-2), module_table (-1)
  lua_rawset(L, -3);                   // sets loaded[modname] = module_table, pops modname (-2) and module_table (-1)
  // Stack now: ... module_table (-2), loaded (-1)
  lua_pop(L, 2);                       // clean up
}

int main() {
  lua_State *L = luaL_newstate();
  if (!L) {
    std::cerr << "Failed to create Lua state" << std::endl;
    return 1;
  }

  luaL_openlibs(L);

  // Load C submodules directly and set in package.loaded
  // LuaSocket C cores
  luaopen_socket_core(L);
  set_module(L, "socket.core");
  std::cout << "pload socket" << std::endl;

  luaopen_mime_core(L);
  set_module(L, "mime.core");

  // LuaSec C submodules
  luaopen_ssl_core(L);
  set_module(L, "ssl.core");

  luaopen_ssl_context(L);
  set_module(L, "ssl.context");

  luaopen_ssl_x509(L);
  set_module(L, "ssl.x509");

  luaopen_ssl_config(L);
  set_module(L, "ssl.config");

  // Load LuaSocket Lua modules (in dependency order, set in loaded)
    try {
        // ltn12.lua
        std::string ltn12_code = load_lua_file("../subprojects/luasocket/src/ltn12.lua");
        if (luaL_loadstring(L, ltn12_code.c_str()) || lua_pcall(L, 0, 1, 0)) {
            std::cerr << "Error loading ltn12.lua: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
            lua_close(L);
            return 1;
        }
        set_module(L, "ltn12");

        // url.lua
        std::string url_code = load_lua_file("../subprojects/luasocket/src/url.lua");
        if (luaL_loadstring(L, url_code.c_str()) || lua_pcall(L, 0, 1, 0)) {
            std::cerr << "Error loading url.lua: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
            lua_close(L);
            return 1;
        }
        set_module(L, "socket.url");

        // mime.lua
        std::string mime_code = load_lua_file("../subprojects/luasocket/src/mime.lua");
        if (luaL_loadstring(L, mime_code.c_str()) || lua_pcall(L, 0, 1, 0)) {
            std::cerr << "Error loading mime.lua: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
            lua_close(L);
            return 1;
        }
        set_module(L, "mime");

        // socket.lua
        std::string socket_code = load_lua_file("../subprojects/luasocket/src/socket.lua");
        if (luaL_loadstring(L, socket_code.c_str()) || lua_pcall(L, 0, 1, 0)) {
            std::cerr << "Error loading socket.lua: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
            lua_close(L);
            return 1;
        }
        set_module(L, "socket");

        // http.lua
        std::string http_code = load_lua_file("../subprojects/luasocket/src/http.lua");
        if (luaL_loadstring(L, http_code.c_str()) || lua_pcall(L, 0, 1, 0)) {
            std::cerr << "Error loading http.lua: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
            lua_close(L);
            return 1;
        }
        set_module(L, "socket.http");
    } catch (const std::exception& e) {
        std::cerr << "LuaSocket file load error: " << e.what() << std::endl;
        lua_close(L);
        return 1;
    }

    // Load LuaSec Lua modules
    try {
        // ssl.lua
        std::string ssl_code = load_lua_file("../subprojects/luasec/src/ssl.lua");
        if (luaL_loadstring(L, ssl_code.c_str()) || lua_pcall(L, 0, 1, 0)) {
            std::cerr << "Error loading ssl.lua: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
            lua_close(L);
            return 1;
        }
        set_module(L, "ssl");

        // https.lua
        std::string https_code = load_lua_file("../subprojects/luasec/src/https.lua");
        if (luaL_loadstring(L, https_code.c_str()) || lua_pcall(L, 0, 1, 0)) {
            std::cerr << "Error loading https.lua: " << lua_tostring(L, -1) << std::endl;
            lua_pop(L, 1);
            lua_close(L);
            return 1;
        }
        set_module(L, "ssl.https");
    } catch (const std::exception& e) {
        std::cerr << "LuaSec file load error: " << e.what() << std::endl;
        lua_close(L);
        return 1;
    }

  const char *lua_script = R"(

    local socket = require("socket")
    local ssl = require("ssl")
local sock = socket.tcp()
        sock:settimeout(10)
sock:connect('www.quizbin.com', 443)
local params = {}
        local ssl_sock = ssl.wrap(sock, params)
        ssl_sock:dohandshake()
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
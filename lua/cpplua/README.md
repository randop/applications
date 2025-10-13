# CppLua
C++ project demo of a robust Lua integration using the native Lua C API.
The Lua script runs in a background thread to prevent blocking the main thread. The Lua script implements a simple timer loop that periodically invokes a registered C++ callback function every second.

## Project Development
1. Install toolchain packages: `pacman -S meson ninja lua`
2. Verify lua headers: `pacman -Ql lua | grep include`
```
lua /usr/include/
lua /usr/include/lauxlib.h
lua /usr/include/lua.h
lua /usr/include/lua.hpp
lua /usr/include/luaconf.h
lua /usr/include/lualib.h
```
3. Setup build: `meson setup build`
4. Compile: `meson compile -C build`
5. Execute: `./build/cpplua`
```
Starting main thread and spawning Lua background thread...
Main thread working...
Called from Lua timer with timestamp: 2025-10-13 11:50:50
Main thread working...
Called from Lua timer with timestamp: 2025-10-13 11:50:51
Main thread working...
Called from Lua timer with timestamp: 2025-10-13 11:50:52
Main thread working...
Called from Lua timer with timestamp: 2025-10-13 11:50:53
Main thread working...
Called from Lua timer with timestamp: 2025-10-13 11:50:54
Main thread working...
Called from Lua timer with timestamp: 2025-10-13 11:50:55
Main thread working...
Called from Lua timer with timestamp: 2025-10-13 11:50:56
Main thread working...
Called from Lua timer with timestamp: 2025-10-13 11:50:57
Main thread working...
Called from Lua timer with timestamp: 2025-10-13 11:50:58
Main thread working...
Called from Lua timer with timestamp: 2025-10-13 11:50:59
Lua timer completed
Lua background thread finished
Main thread exiting
```

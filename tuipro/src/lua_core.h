#ifndef TUIPRO_LUA_CORE_H
#define TUIPRO_LUA_CORE_H

#include <lua.h>

int luaopen_tuipro_core(lua_State *L);
void tuipro_core_force_shutdown(void);
void tuipro_core_free(void);

#endif

#include "lua_core.h"
#include "tuipro_internal.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <stdlib.h>
#include <string.h>

static tp_term *g_t = NULL;

static tp_term *check_term(lua_State *L)
{
  if (!g_t || !tp_term_active(g_t))
    luaL_error(L, "tuipro core is not initialized");
  return g_t;
}

static uint32_t opt_color(lua_State *L, int idx)
{
  if (lua_isnoneornil(L, idx))
    return TP_DEFAULT_COLOR;
  lua_Number n = luaL_checknumber(L, idx);
  if (n < 0)
    return TP_DEFAULT_COLOR;
  return (uint32_t)n;
}

static int opt_attrs(lua_State *L, int idx)
{
  if (lua_isnoneornil(L, idx))
    return 0;
  return (int)luaL_optinteger(L, idx, 0);
}

static tp_cursor_shape parse_shape(lua_State *L, int idx, tp_cursor_shape def)
{
  if (lua_isnoneornil(L, idx))
    return def;
  if (lua_type(L, idx) == LUA_TNUMBER) {
    int v = (int)lua_tointeger(L, idx);
    if (v < 0 || v > 3)
      return TP_CURSOR_DEFAULT;
    return (tp_cursor_shape)v;
  }
  const char *s = luaL_checkstring(L, idx);
  if (strcmp(s, "block") == 0)
    return TP_CURSOR_BLOCK;
  if (strcmp(s, "underline") == 0 || strcmp(s, "under") == 0)
    return TP_CURSOR_UNDERLINE;
  if (strcmp(s, "bar") == 0 || strcmp(s, "line") == 0)
    return TP_CURSOR_BAR;
  return TP_CURSOR_DEFAULT;
}

static int l_init(lua_State *L)
{
  bool headless = false;
  int width = 0, height = 0;
  int truecolor = -1;
  bool mouse = false;
  bool paste = true;
  const char *title = NULL;

  if (!lua_isnoneornil(L, 1)) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "headless");
    if (!lua_isnil(L, -1))
      headless = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "width");
    if (!lua_isnil(L, -1))
      width = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "height");
    if (!lua_isnil(L, -1))
      height = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "truecolor");
    if (!lua_isnil(L, -1))
      truecolor = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);
    lua_getfield(L, 1, "mouse");
    if (!lua_isnil(L, -1))
      mouse = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "paste");
    if (!lua_isnil(L, -1))
      paste = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "title");
    if (!lua_isnil(L, -1))
      title = lua_tostring(L, -1);
    lua_pop(L, 1);
  }

  if (!g_t) {
    g_t = tp_term_new();
    if (!g_t)
      return luaL_error(L, "out of memory");
  }
  if (tp_term_init(g_t, headless, width, height, truecolor) < 0) {
    lua_pushfstring(L, "%s", tp_term_error(g_t));
    return lua_error(L);
  }
  tp_term_set_paste(g_t, paste);
  tp_term_set_mouse(g_t, mouse);
  if (title)
    tp_term_set_title(g_t, title);
  lua_pushboolean(L, 1);
  return 1;
}

static int l_shutdown(lua_State *L)
{
  if (g_t)
    tp_term_shutdown(g_t);
  lua_pushboolean(L, 1);
  return 1;
}

static int l_initialized(lua_State *L)
{
  lua_pushboolean(L, g_t && tp_term_active(g_t));
  return 1;
}

static int l_size(lua_State *L)
{
  tp_term *t = check_term(L);
  lua_pushinteger(L, tp_term_width(t));
  lua_pushinteger(L, tp_term_height(t));
  return 2;
}

static int l_put(lua_State *L)
{
  tp_term *t = check_term(L);
  int x = (int)luaL_checkinteger(L, 1);
  int y = (int)luaL_checkinteger(L, 2);
  const char *text = luaL_checkstring(L, 3);
  uint32_t fg = opt_color(L, 4);
  uint32_t bg = opt_color(L, 5);
  int attrs = opt_attrs(L, 6);
  int nx = tp_grid_put(t, x, y, text, fg, bg, (uint16_t)attrs);
  lua_pushinteger(L, nx);
  return 1;
}

static int l_fill(lua_State *L)
{
  tp_term *t = check_term(L);
  int x = (int)luaL_checkinteger(L, 1);
  int y = (int)luaL_checkinteger(L, 2);
  int w = (int)luaL_checkinteger(L, 3);
  int h = (int)luaL_checkinteger(L, 4);
  const char *text = luaL_checkstring(L, 5);
  uint32_t fg = opt_color(L, 6);
  uint32_t bg = opt_color(L, 7);
  int attrs = opt_attrs(L, 8);
  tp_grid_fill(t, x, y, w, h, text, fg, bg, (uint16_t)attrs);
  return 0;
}

static int l_clip(lua_State *L)
{
  tp_term *t = check_term(L);
  int x = (int)luaL_checkinteger(L, 1);
  int y = (int)luaL_checkinteger(L, 2);
  int w = (int)luaL_checkinteger(L, 3);
  int h = (int)luaL_checkinteger(L, 4);
  tp_grid_set_clip(t, x, y, w, h);
  return 0;
}

static int l_clip_reset(lua_State *L)
{
  tp_term *t = check_term(L);
  tp_grid_reset_clip(t);
  return 0;
}

static int l_clear(lua_State *L)
{
  tp_term *t = check_term(L);
  tp_grid_clear(t);
  return 0;
}

static int l_flush(lua_State *L)
{
  tp_term *t = check_term(L);
  tp_grid_flush(t);
  return 0;
}

static int l_cursor(lua_State *L)
{
  tp_term *t = check_term(L);
  int x = (int)luaL_optinteger(L, 1, -1);
  int y = (int)luaL_optinteger(L, 2, -1);
  bool visible = true;
  if (!lua_isnoneornil(L, 3))
    visible = lua_toboolean(L, 3);
  tp_cursor_shape shape = parse_shape(L, 4, TP_CURSOR_BLOCK);
  tp_term_set_cursor(t, x, y, visible, shape);
  return 0;
}

static int l_rows(lua_State *L)
{
  tp_term *t = check_term(L);
  int h = tp_term_height(t);
  lua_createtable(L, h, 0);
  for (int y = 0; y < h; y++) {
    char *row = tp_grid_row_alloc(t, y, false);
    if (row)
      lua_pushstring(L, row);
    else
      lua_pushliteral(L, "");
    free(row);
    lua_rawseti(L, -2, y + 1);
  }
  return 1;
}

static int l_snapshot(lua_State *L)
{
  tp_term *t = check_term(L);
  int h = tp_term_height(t);
  lua_createtable(L, h, 0);
  for (int y = 0; y < h; y++) {
    char *row = tp_grid_row_alloc(t, y, true);
    if (row)
      lua_pushstring(L, row);
    else
      lua_pushliteral(L, "");
    free(row);
    lua_rawseti(L, -2, y + 1);
  }
  return 1;
}

static int l_cell(lua_State *L)
{
  tp_term *t = check_term(L);
  int x = (int)luaL_checkinteger(L, 1);
  int y = (int)luaL_checkinteger(L, 2);
  tp_cell c;
  if (!tp_grid_cell_at(t, x, y, &c)) {
    lua_pushnil(L);
    return 1;
  }
  char text[16] = { 0 };
  size_t len = 0;
  if (c.width != 0) {
    uint32_t cp = c.codepoint ? c.codepoint : ' ';
    char enc[4];
    int n = tp_utf8_encode(cp, enc);
    memcpy(text, enc, (size_t)n);
    len = (size_t)n;
    for (int i = 0; i < 2; i++) {
      if (!c.combining[i])
        break;
      n = tp_utf8_encode(c.combining[i], enc);
      memcpy(text + len, enc, (size_t)n);
      len += (size_t)n;
    }
  }
  lua_pushlstring(L, text, len);
  lua_pushnumber(L, c.fg == TP_DEFAULT_COLOR ? -1 : (lua_Number)c.fg);
  lua_pushnumber(L, c.bg == TP_DEFAULT_COLOR ? -1 : (lua_Number)c.bg);
  lua_pushinteger(L, c.attrs);
  lua_pushinteger(L, c.width);
  return 5;
}

static int l_text_width(lua_State *L)
{
  const char *s = luaL_checkstring(L, 1);
  lua_pushinteger(L, tp_text_width(s));
  return 1;
}

static const char *mouse_action_name(tp_mouse_action a)
{
  switch (a) {
  case TP_MOUSE_PRESS: return "press";
  case TP_MOUSE_RELEASE: return "release";
  case TP_MOUSE_MOVE: return "move";
  case TP_MOUSE_WHEEL: return "wheel";
  default: return "none";
  }
}

static void set_mods(lua_State *L, int mods)
{
  lua_createtable(L, 0, 4);
  lua_pushboolean(L, !!(mods & TP_MOD_CTRL));
  lua_setfield(L, -2, "ctrl");
  lua_pushboolean(L, !!(mods & TP_MOD_ALT));
  lua_setfield(L, -2, "alt");
  lua_pushboolean(L, !!(mods & TP_MOD_SHIFT));
  lua_setfield(L, -2, "shift");
  lua_pushboolean(L, !!(mods & TP_MOD_SUPER));
  lua_setfield(L, -2, "super");
  lua_pushinteger(L, mods);
  lua_setfield(L, -2, "value");
}

static void push_event(lua_State *L, const tp_event *ev)
{
  lua_createtable(L, 0, 10);
  switch (ev->type) {
  case TP_EVENT_KEY:
    lua_pushliteral(L, "key");
    lua_setfield(L, -2, "type");
    lua_pushstring(L, ev->key);
    lua_setfield(L, -2, "key");
    if (ev->text[0]) {
      lua_pushstring(L, ev->text);
      lua_setfield(L, -2, "text");
    }
    set_mods(L, ev->mods);
    lua_setfield(L, -2, "mods");
    break;
  case TP_EVENT_MOUSE:
    lua_pushliteral(L, "mouse");
    lua_setfield(L, -2, "type");
    lua_pushstring(L, ev->button);
    lua_setfield(L, -2, "button");
    lua_pushstring(L, mouse_action_name(ev->action));
    lua_setfield(L, -2, "action");
    lua_pushinteger(L, ev->x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, ev->y);
    lua_setfield(L, -2, "y");
    set_mods(L, ev->mods);
    lua_setfield(L, -2, "mods");
    break;
  case TP_EVENT_RESIZE:
    lua_pushliteral(L, "resize");
    lua_setfield(L, -2, "type");
    lua_pushinteger(L, ev->width);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, ev->height);
    lua_setfield(L, -2, "height");
    break;
  case TP_EVENT_PASTE:
    lua_pushliteral(L, "paste");
    lua_setfield(L, -2, "type");
    if (ev->data) {
      lua_pushlstring(L, ev->data, ev->data_len);
      lua_setfield(L, -2, "text");
    } else {
      lua_pushliteral(L, "");
      lua_setfield(L, -2, "text");
    }
    break;
  case TP_EVENT_SIGNAL:
    lua_pushliteral(L, "signal");
    lua_setfield(L, -2, "type");
    lua_pushstring(L, ev->signal);
    lua_setfield(L, -2, "name");
    break;
  case TP_EVENT_FOCUS:
    lua_pushliteral(L, "focus");
    lua_setfield(L, -2, "type");
    lua_pushboolean(L, ev->action != TP_MOUSE_RELEASE);
    lua_setfield(L, -2, "focused");
    break;
  default:
    lua_pushliteral(L, "none");
    lua_setfield(L, -2, "type");
    break;
  }
}

static int l_poll(lua_State *L)
{
  tp_term *t = check_term(L);
  int timeout = (int)luaL_optinteger(L, 1, 0);
  tp_event ev;
  int rc = tp_term_poll(t, timeout, &ev);
  if (rc < 0) {
    lua_pushnil(L);
    return 1;
  }
  if (rc == 0) {
    lua_pushnil(L);
    return 1;
  }
  push_event(L, &ev);
  tp_event_release(&ev);
  return 1;
}

static int l_exit(lua_State *L)
{
  if (g_t)
    tp_term_request_exit(g_t);
  return 0;
}

static int l_should_exit(lua_State *L)
{
  lua_pushboolean(L, g_t && tp_term_exit_requested(g_t));
  return 1;
}

static int l_title(lua_State *L)
{
  tp_term *t = check_term(L);
  const char *title = luaL_checkstring(L, 1);
  tp_term_set_title(t, title);
  return 0;
}

static int l_bell(lua_State *L)
{
  tp_term *t = check_term(L);
  tp_term_bell(t);
  return 0;
}

static int l_copy(lua_State *L)
{
  tp_term *t = check_term(L);
  const char *s = luaL_checkstring(L, 1);
  tp_term_copy(t, s);
  return 0;
}

static int l_set_mouse(lua_State *L)
{
  tp_term *t = check_term(L);
  tp_term_set_mouse(t, lua_toboolean(L, 1) != 0);
  return 0;
}

static int l_set_paste(lua_State *L)
{
  tp_term *t = check_term(L);
  tp_term_set_paste(t, lua_toboolean(L, 1) != 0);
  return 0;
}

static int l_ticks(lua_State *L)
{
  lua_pushnumber(L, tp_now_ms());
  return 1;
}

static int l_resize(lua_State *L)
{
  tp_term *t = check_term(L);
  int w = (int)luaL_checkinteger(L, 1);
  int h = (int)luaL_checkinteger(L, 2);
  if (w < 1 || h < 1)
    return luaL_error(L, "invalid size");
  tp_grid_resize(t, w, h);
  return 0;
}

static int l_info(lua_State *L)
{
  tp_term *t = check_term(L);
  lua_createtable(L, 0, 8);
  lua_pushliteral(L, TUIPRO_VERSION);
  lua_setfield(L, -2, "version");
  lua_pushliteral(L, LUA_VERSION);
  lua_setfield(L, -2, "lua_version");
  lua_pushboolean(L, tp_term_truecolor(t));
  lua_setfield(L, -2, "truecolor");
  lua_pushinteger(L, tp_term_width(t));
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, tp_term_height(t));
  lua_setfield(L, -2, "height");
  const char *term = getenv("TERM");
  lua_pushstring(L, term ? term : "");
  lua_setfield(L, -2, "term");
  const char *prog = getenv("TERM_PROGRAM");
  lua_pushstring(L, prog ? prog : "");
  lua_setfield(L, -2, "term_program");
  return 1;
}

static const luaL_Reg core_funcs[] = {
  { "init", l_init },
  { "shutdown", l_shutdown },
  { "initialized", l_initialized },
  { "size", l_size },
  { "put", l_put },
  { "fill", l_fill },
  { "clip", l_clip },
  { "clip_reset", l_clip_reset },
  { "clear", l_clear },
  { "flush", l_flush },
  { "cursor", l_cursor },
  { "rows", l_rows },
  { "snapshot", l_snapshot },
  { "cell", l_cell },
  { "text_width", l_text_width },
  { "poll", l_poll },
  { "exit", l_exit },
  { "should_exit", l_should_exit },
  { "title", l_title },
  { "bell", l_bell },
  { "copy", l_copy },
  { "set_mouse", l_set_mouse },
  { "set_paste", l_set_paste },
  { "ticks", l_ticks },
  { "resize", l_resize },
  { "info", l_info },
  { NULL, NULL },
};

int luaopen_tuipro_core(lua_State *L)
{
  luaL_newlib(L, core_funcs);
  lua_pushliteral(L, TUIPRO_VERSION);
  lua_setfield(L, -2, "version");
  lua_pushliteral(L, "LuaJIT");
  lua_setfield(L, -2, "engine");
  return 1;
}

void tuipro_core_force_shutdown(void)
{
  if (g_t)
    tp_term_shutdown(g_t);
}

void tuipro_core_free(void)
{
  if (g_t) {
    tp_term_free(g_t);
    g_t = NULL;
  }
}

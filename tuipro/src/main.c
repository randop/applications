#include "lua_core.h"
#include "tuipro.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef TUIPRO_LUA_DIR
#define TUIPRO_LUA_DIR "/usr/local/share/tuipro/lua"
#endif

typedef struct {
  bool headless;
  int width;
  int height;
  int fps;
  int frames;
  int truecolor;  /* -1 auto, 0 off, 1 on */
  bool mouse;
  bool paste;
  const char *script;
  const char *module;
  const char *plugin;
  const char *eval;
  bool demo;
  bool demo_coding;
  int arg_start;
} cli_options;

static const char *progname = "tuipro";
static bool g_lua_error = false;

static void usage(FILE *out)
{
  fprintf(out,
    "tuipro %s - terminal UI container with Lua widgets\n"
    "\n"
    "Usage:\n"
    "  tuipro [options] [script.lua | module] [args...]\n"
    "  tuipro --plugin plugins/hello\n"
    "  tuipro --demo\n"
    "  tuipro --demo-coding\n"
    "\n"
    "Options:\n"
    "  -h, --help             show this help\n"
    "  -v, --version          show version\n"
    "      --demo             run the built-in dashboard demo\n"
    "      --demo-coding      run the built-in coding demo (opencode-style)\n"
    "      --plugin PATH      run a plugin (directory with init.lua, or a file)\n"
    "  -l, --load MODULE      require a Lua module and run its entry point\n"
    "  -e, --eval CODE        evaluate Lua code\n"
    "      --headless         no terminal I/O (for tests and scripts)\n"
    "      --size WxH         initial size for headless mode (default 80x24)\n"
    "      --fps N            maximum redraw rate (default 60)\n"
    "      --frames N         exit after N rendered frames (smoke tests)\n"
    "      --truecolor        force 24-bit colors\n"
    "      --no-truecolor     force 256-color output\n"
    "      --mouse            enable mouse reporting (default)\n"
    "      --no-mouse         disable mouse reporting\n"
    "      --no-paste         disable bracketed paste\n"
    "\n"
    "Environment:\n"
    "  TUIPRO_RUNTIME     path containing tuipro Lua modules (the 'lua' dir)\n"
    "  TUIPRO_TRUECOLOR   set to 0 to disable truecolor\n"
    "  TUIPRO_SYNC        set to 0 to disable synchronized output\n"
    "\n"
    "Examples:\n"
    "  tuipro examples/dashboard.lua\n"
    "  tuipro --plugin plugins/hello\n"
    "  tuipro --headless --size 80x24 tests/test_widgets.lua\n",
    TUIPRO_VERSION_STRING);
}

static void print_version(void)
{
  printf("tuipro %s (LuaJIT %s)\n", TUIPRO_VERSION_STRING, "2.1");
}

static bool parse_size(const char *s, int *w, int *h)
{
  char *end = NULL;
  long width = strtol(s, &end, 10);
  if (!end || (*end != 'x' && *end != 'X'))
    return false;
  const char *hs = end + 1;
  long height = strtol(hs, &end, 10);
  if (!end || *end != '\0' || width <= 0 || height <= 0)
    return false;
  *w = (int)width;
  *h = (int)height;
  return true;
}

static int parse_args(int argc, char **argv, cli_options *opts)
{
  memset(opts, 0, sizeof(*opts));
  opts->fps = 60;
  opts->truecolor = -1;
  opts->mouse = true;
  opts->paste = true;
  opts->arg_start = argc;

  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (strcmp(a, "--") == 0) {
      opts->arg_start = i + 1;
      if (i + 1 < argc)
        opts->script = argv[i + 1];
      break;
    }
    if (a[0] != '-' || strcmp(a, "-") == 0) {
      opts->script = a;
      opts->arg_start = i + 1;
      break;
    }
    if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
      usage(stdout);
      exit(0);
    } else if (strcmp(a, "-v") == 0 || strcmp(a, "--version") == 0) {
      print_version();
      exit(0);
    } else if (strcmp(a, "--demo") == 0) {
      opts->demo = true;
      opts->demo_coding = false;
    } else if (strcmp(a, "--demo-coding") == 0) {
      opts->demo_coding = true;
      opts->demo = false;
    } else if (strcmp(a, "--headless") == 0) {
      opts->headless = true;
    } else if (strcmp(a, "--truecolor") == 0) {
      opts->truecolor = 1;
    } else if (strcmp(a, "--no-truecolor") == 0) {
      opts->truecolor = 0;
    } else if (strcmp(a, "--mouse") == 0) {
      opts->mouse = true;
    } else if (strcmp(a, "--no-mouse") == 0) {
      opts->mouse = false;
    } else if (strcmp(a, "--no-paste") == 0) {
      opts->paste = false;
    } else if (strcmp(a, "--size") == 0) {
      if (++i >= argc || !parse_size(argv[i], &opts->width, &opts->height)) {
        fprintf(stderr, "%s: --size expects WxH\n", progname);
        return -1;
      }
    } else if (strncmp(a, "--size=", 7) == 0) {
      if (!parse_size(a + 7, &opts->width, &opts->height)) {
        fprintf(stderr, "%s: --size expects WxH\n", progname);
        return -1;
      }
    } else if (strcmp(a, "--fps") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "%s: --fps expects a number\n", progname);
        return -1;
      }
      opts->fps = atoi(argv[i]);
      if (opts->fps < 1)
        opts->fps = 1;
      if (opts->fps > 240)
        opts->fps = 240;
    } else if (strcmp(a, "--frames") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "%s: --frames expects a number\n", progname);
        return -1;
      }
      opts->frames = atoi(argv[i]);
    } else if (strcmp(a, "--plugin") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "%s: --plugin expects a path or name\n", progname);
        return -1;
      }
      opts->plugin = argv[i];
      opts->arg_start = i + 1;
      break;
    } else if (strncmp(a, "--plugin=", 9) == 0) {
      opts->plugin = a + 9;
      opts->arg_start = i + 1;
      break;
    } else if (strcmp(a, "-l") == 0 || strcmp(a, "--load") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "%s: -l expects a module name\n", progname);
        return -1;
      }
      opts->module = argv[i];
    } else if (strcmp(a, "-e") == 0 || strcmp(a, "--eval") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "%s: -e expects Lua code\n", progname);
        return -1;
      }
      opts->eval = argv[i];
    } else {
      fprintf(stderr, "%s: unknown option '%s' (try --help)\n", progname, a);
      return -1;
    }
  }
  return 0;
}

/* ------------------------------------------------------------------------- */
/* Lua helpers                                                               */
/* ------------------------------------------------------------------------- */

static int msgh(lua_State *L)
{
  const char *msg = lua_tostring(L, 1);
  if (msg)
    luaL_traceback(L, L, msg, 1);
  else
    lua_pushliteral(L, "(error object is not a string)");
  return 1;
}

static int push_protected_call(lua_State *L, int nargs, const char *what)
{
  int base = lua_gettop(L) - nargs;
  lua_pushcfunction(L, msgh);
  lua_insert(L, base);
  int rc = lua_pcall(L, nargs, LUA_MULTRET, base);
  if (rc != 0) {
    const char *err = lua_tostring(L, -1);
    fprintf(stderr, "%s: %s\n", what, err ? err : "unknown error");
    lua_pop(L, 1);
    g_lua_error = true;
    return -1;
  }
  lua_remove(L, base);
  return 0;
}

static void run_returned_value(lua_State *L, int idx);

static void call_entry(lua_State *L, const char *what)
{
  /* Function on top: call f(tuipro, cli) */
  lua_getglobal(L, "tuipro");
  lua_getglobal(L, "tuipro_cli");
  if (push_protected_call(L, 2, what) < 0)
    return;
  if (lua_gettop(L) > 0)
    run_returned_value(L, -1);
}

static void run_returned_value(lua_State *L, int idx)
{
  /* Convert to an absolute index: the stack changes below, and a relative
   * index would start pointing at newly pushed values. */
  if (idx < 0)
    idx = lua_gettop(L) + idx + 1;
  if (lua_isnumber(L, idx)) {
    int code = (int)lua_tonumber(L, idx);
    if (code != 0) {
      tuipro_core_force_shutdown();
      exit(code);
    }
    return;
  }
  if (lua_isfunction(L, idx)) {
    lua_pushvalue(L, idx);
    call_entry(L, "entry point");
    return;
  }
  if (lua_istable(L, idx)) {
    lua_getfield(L, idx, "run");
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, idx);
      if (push_protected_call(L, 1, "run()") < 0)
        return;
      return;
    }
    lua_pop(L, 1);
    lua_getfield(L, idx, "setup");
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, idx);
      lua_getglobal(L, "tuipro");
      lua_getglobal(L, "tuipro_cli");
      if (push_protected_call(L, 3, "setup()") < 0)
        return;
      return;
    }
    lua_pop(L, 1);
  }
}

static char *read_exe_dir(void)
{
  char buf[PATH_MAX];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0)
    return NULL;
  buf[n] = '\0';
  char *slash = strrchr(buf, '/');
  if (!slash)
    return NULL;
  *slash = '\0';
  return strdup(buf);
}

static void prepend_package_path(lua_State *L, const char *dir)
{
  if (!dir || !*dir)
    return;
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "path");
  const char *old = lua_tostring(L, -1);
  char *needed;
  if (asprintf(&needed, "%s/?.lua;%s/?/init.lua", dir, dir) < 0)
    return;
  lua_pop(L, 1);
  lua_pushfstring(L, "%s;%s", needed, old ? old : "");
  lua_setfield(L, -2, "path");
  lua_pop(L, 1);
  free(needed);
}

static void setup_package_path(lua_State *L)
{
  const char *runtime = getenv("TUIPRO_RUNTIME");
  if (runtime && *runtime) {
    prepend_package_path(L, runtime);
    char *lua_dir;
    if (asprintf(&lua_dir, "%s/lua", runtime) >= 0) {
      prepend_package_path(L, lua_dir);
      free(lua_dir);
    }
  }

  char *exe_dir = read_exe_dir();
  if (exe_dir) {
    char *p;
    if (asprintf(&p, "%s/../share/tuipro/lua", exe_dir) >= 0) {
      prepend_package_path(L, p);
      free(p);
    }
    if (asprintf(&p, "%s/../lua", exe_dir) >= 0) {
      prepend_package_path(L, p);
      free(p);
    }
    if (asprintf(&p, "%s/lua", exe_dir) >= 0) {
      prepend_package_path(L, p);
      free(p);
    }
    free(exe_dir);
  }

  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd))) {
    char *p;
    if (asprintf(&p, "%s/lua", cwd) >= 0) {
      prepend_package_path(L, p);
      free(p);
    }
  }

  prepend_package_path(L, TUIPRO_LUA_DIR);
}

static void set_arg_table(lua_State *L, int argc, char **argv, int arg_start,
                          const char *script)
{
  lua_createtable(L, argc, 4);
  lua_pushstring(L, script ? script : progname);
  lua_rawseti(L, -2, 0);
  int n = 1;
  for (int i = arg_start; i < argc; i++) {
    lua_pushstring(L, argv[i]);
    lua_rawseti(L, -2, n++);
  }
  lua_setglobal(L, "arg");
}

static void set_cli_table(lua_State *L, const cli_options *opts,
                          int argc, char **argv)
{
  lua_createtable(L, 0, 16);
  lua_pushboolean(L, opts->headless);
  lua_setfield(L, -2, "headless");
  lua_pushinteger(L, opts->width);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, opts->height);
  lua_setfield(L, -2, "height");
  lua_pushinteger(L, opts->fps);
  lua_setfield(L, -2, "fps");
  lua_pushinteger(L, opts->frames);
  lua_setfield(L, -2, "frames");
  if (opts->truecolor < 0)
    lua_pushnil(L);
  else
    lua_pushboolean(L, opts->truecolor != 0);
  lua_setfield(L, -2, "truecolor");
  lua_pushboolean(L, opts->mouse);
  lua_setfield(L, -2, "mouse");
  lua_pushboolean(L, opts->paste);
  lua_setfield(L, -2, "paste");
  if (opts->plugin) {
    lua_pushstring(L, opts->plugin);
    lua_setfield(L, -2, "plugin");
  }
  if (opts->script) {
    lua_pushstring(L, opts->script);
    lua_setfield(L, -2, "script");
  }
  lua_createtable(L, argc, 0);
  int n = 1;
  for (int i = opts->arg_start; i < argc; i++) {
    lua_pushstring(L, argv[i]);
    lua_rawseti(L, -2, n++);
  }
  lua_setfield(L, -2, "args");
  lua_setglobal(L, "tuipro_cli");
}

static int file_exists(const char *path)
{
  return access(path, R_OK) == 0;
}

static char *resolve_plugin(const char *name)
{
  if (!name)
    return NULL;
  struct stat st;
  if (stat(name, &st) == 0) {
    if (S_ISDIR(st.st_mode)) {
      char *p;
      if (asprintf(&p, "%s/init.lua", name) >= 0 && file_exists(p))
        return p;
      free(p);
      return NULL;
    }
    if (S_ISREG(st.st_mode))
      return strdup(name);
  }
  /* Search plugins/<name>/init.lua relative to cwd, runtime and install. */
  const char *roots[8];
  char cwd[PATH_MAX];
  static char cwd_plugins[PATH_MAX + 16];
  static char rt_plugins[PATH_MAX + 16];
  static char installed_plugins[PATH_MAX + 16];
  static char exe_share_plugins[PATH_MAX + 64];
  static char exe_plugins[PATH_MAX + 16];
  static char exe_self_plugins[PATH_MAX + 16];
  int nr = 0;
  if (getcwd(cwd, sizeof(cwd))) {
    snprintf(cwd_plugins, sizeof(cwd_plugins), "%s/plugins", cwd);
    roots[nr++] = cwd_plugins;
  }
  const char *runtime = getenv("TUIPRO_RUNTIME");
  if (runtime && *runtime) {
    snprintf(rt_plugins, sizeof(rt_plugins), "%s/plugins", runtime);
    roots[nr++] = rt_plugins;
  }
  /* The executable may have been installed with a prefix that differs from
   * the compile-time one (cmake --install --prefix).  Search next to it too,
   * mirroring the Lua package path probing in setup_package_path(). */
  char *exe_dir = read_exe_dir();
  if (exe_dir) {
    snprintf(exe_share_plugins, sizeof(exe_share_plugins),
             "%s/../share/tuipro/plugins", exe_dir);
    roots[nr++] = exe_share_plugins;
    snprintf(exe_plugins, sizeof(exe_plugins), "%s/../plugins", exe_dir);
    roots[nr++] = exe_plugins;
    snprintf(exe_self_plugins, sizeof(exe_self_plugins), "%s/plugins", exe_dir);
    roots[nr++] = exe_self_plugins;
  }
  snprintf(installed_plugins, sizeof(installed_plugins), "%s/../plugins", TUIPRO_LUA_DIR);
  roots[nr++] = installed_plugins;
  roots[nr] = NULL;
  for (int i = 0; i < nr; i++) {
    char *p;
    if (asprintf(&p, "%s/%s/init.lua", roots[i], name) >= 0) {
      if (file_exists(p)) {
        free(exe_dir);
        return p;
      }
      free(p);
    }
    if (asprintf(&p, "%s/%s.lua", roots[i], name) >= 0) {
      if (file_exists(p)) {
        free(exe_dir);
        return p;
      }
      free(p);
    }
  }
  free(exe_dir);
  return NULL;
}

static int run_chunk_file(lua_State *L, const char *path, const char *label,
                          int nargs_hint)
{
  if (luaL_loadfile(L, path) != 0) {
    const char *err = lua_tostring(L, -1);
    fprintf(stderr, "%s: %s\n", label, err ? err : "load error");
    lua_pop(L, 1);
    return -1;
  }
  if (push_protected_call(L, 0, label) < 0)
    return -1;
  if (lua_gettop(L) > 0)
    run_returned_value(L, -1);
  (void)nargs_hint;
  return 0;
}

static int run_eval(lua_State *L, const char *code)
{
  if (luaL_loadstring(L, code) != 0) {
    const char *err = lua_tostring(L, -1);
    fprintf(stderr, "eval: %s\n", err ? err : "load error");
    lua_pop(L, 1);
    return -1;
  }
  if (push_protected_call(L, 0, "eval") < 0)
    return -1;
  if (lua_gettop(L) > 0)
    run_returned_value(L, -1);
  return 0;
}

static void require_entry(lua_State *L, const char *module)
{
  lua_getglobal(L, "require");
  lua_pushstring(L, module);
  if (push_protected_call(L, 1, "require") < 0)
    return;
  if (lua_gettop(L) > 0)
    run_returned_value(L, -1);
}

int main(int argc, char **argv)
{
  if (argc > 0 && argv[0] && *argv[0]) {
    const char *slash = strrchr(argv[0], '/');
    progname = slash ? slash + 1 : argv[0];
  }
  setlocale(LC_CTYPE, "");
  if (!setlocale(LC_CTYPE, ""))
    setlocale(LC_CTYPE, "C.UTF-8");

  cli_options opts;
  if (parse_args(argc, argv, &opts) < 0)
    return 2;

  lua_State *L = luaL_newstate();
  if (!L) {
    fprintf(stderr, "%s: cannot create Lua state\n", progname);
    return 1;
  }
  luaL_openlibs(L);

  /* Register the C core as tuipro.core (no separate shared object needed). */
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "preload");
  lua_pushcfunction(L, luaopen_tuipro_core);
  lua_setfield(L, -2, "tuipro.core");
  lua_pop(L, 2);

  setup_package_path(L);
  set_arg_table(L, argc, argv, opts.arg_start, opts.script);
  set_cli_table(L, &opts, argc, argv);

  /* Load the Lua front-end and expose it as a global for simple scripts. */
  lua_getglobal(L, "require");
  lua_pushliteral(L, "tuipro");
  if (push_protected_call(L, 1, "load tuipro") < 0) {
    tuipro_core_force_shutdown();
    lua_close(L);
    tuipro_core_free();
    return 1;
  }
  lua_setglobal(L, "tuipro");

  int rc = 0;
  if (opts.eval) {
    if (run_eval(L, opts.eval) < 0)
      rc = 1;
  } else if (opts.plugin) {
    char *path = resolve_plugin(opts.plugin);
    if (!path) {
      fprintf(stderr, "%s: plugin not found: %s\n", progname, opts.plugin);
      rc = 1;
    } else {
      char *dir = strdup(path);
      char *slash = strrchr(dir, '/');
      if (slash) {
        *slash = '\0';
        prepend_package_path(L, dir);
      }
      free(dir);
      if (run_chunk_file(L, path, "plugin", 0) < 0)
        rc = 1;
      free(path);
    }
  } else if (opts.module) {
    require_entry(L, opts.module);
  } else if (opts.script) {
    if (file_exists(opts.script)) {
      if (run_chunk_file(L, opts.script, opts.script, 0) < 0)
        rc = 1;
    } else if (strchr(opts.script, '/') == NULL) {
      require_entry(L, opts.script);
    } else {
      fprintf(stderr, "%s: script not found: %s\n", progname, opts.script);
      rc = 1;
    }
  } else {
    require_entry(L, opts.demo_coding ? "tuipro.demo_coding" : "tuipro.demo");
  }

  if (g_lua_error && rc == 0)
    rc = 1;
  tuipro_core_force_shutdown();
  lua_close(L);
  tuipro_core_free();
  return rc;
}

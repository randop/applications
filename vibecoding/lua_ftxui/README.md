# lua_ftxui

A **barebones C++20** project that bridges [FTXUI](https://github.com/ArthurSonzogni/FTXUI) (a terminal UI library) with **Lua 5.4**, letting you script and compose interactive TUI layouts entirely from Lua.

---

## Project layout

```
lua_ftxui/
├── CMakeLists.txt          # FetchContent: FTXUI v5 + Lua 5.4 (static)
├── src/
│   ├── lua_ftxui.hpp       # Store + bridge API declaration
│   ├── lua_ftxui.cpp       # All Lua ↔ FTXUI glue (luaL_Reg table)
│   └── main.cpp            # Boot Lua, run script, start FTXUI event loop
└── scripts/
    └── ui.lua              # Demo script — edit freely, no recompile needed
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  main.cpp                                                   │
│  1. luaL_newstate() + luaL_openlibs()                       │
│  2. lua_ftxui::register_ftxui(L, store)  ← injects API     │
│  3. luaL_dofile(L, "scripts/ui.lua")     ← runs Lua script │
│  4. screen.Loop(store.root)              ← FTXUI event loop │
└──────────────────────┬──────────────────────────────────────┘
                       │ calls
┌──────────────────────▼──────────────────────────────────────┐
│  lua_ftxui.cpp  (the bridge)                                │
│                                                             │
│  Store (integer-keyed):                                     │
│    elements_   : Handle → ftxui::Element                   │
│    components_ : Handle → ComponentEntry                   │
│                           { Component,                      │
│                             InputState*,                    │
│                             CheckboxState*,                 │
│                             RadioboxState* }                │
│    root        : ftxui::Component  (set by set_root)       │
│    quit_requested : bool                                    │
└──────────────────────┬──────────────────────────────────────┘
                       │ luaL_Reg table exposed as `ftxui.*`
┌──────────────────────▼──────────────────────────────────────┐
│  scripts/ui.lua                                             │
│                                                             │
│  ftxui.input / ftxui.button / ftxui.checkbox …             │
│  ftxui.renderer(comp, fn)  ← custom per-frame render fn   │
│  ftxui.set_root(handle)    ← hands root to C++            │
└─────────────────────────────────────────────────────────────┘
```

**Key design decisions**

| Decision | Rationale |
|---|---|
| Integer handles instead of raw pointers | Lua GC can't manage C++ objects; handles are safe integers |
| `ComponentEntry` owns shared state | Component lambdas reference shared_ptr state; no dangling refs |
| Lua callbacks stored in the registry (`luaL_ref`) | Survives GC; callable from any C++ thread/closure |
| `ftxui.renderer(comp, fn)` | Lets Lua drive the DOM every frame while C++ owns focus/events |
| `quit_requested` flag + `CatchEvent` | Safe quit path — no FTXUI internals abused |

---

## Lua API reference

### Element constructors (pure DOM, no focus)

```lua
h = ftxui.text("hello")
h = ftxui.separator()
h = ftxui.hbox({ h1, h2, h3 })
h = ftxui.vbox({ h1, h2, h3 })
h = ftxui.border(h)
h = ftxui.flex(h)            -- expands to fill available space
```

### Component constructors (interactive, participate in focus chain)

```lua
h = ftxui.button("label", function() ... end)
h = ftxui.input("placeholder")
h = ftxui.checkbox("label", initial_bool)
h = ftxui.radiobox({ "opt1", "opt2", "opt3" })
h = ftxui.container_vert({ h1, h2, h3 })   -- vertical focus chain
h = ftxui.container_horiz({ h1, h2, h3 })  -- horizontal focus chain
h = ftxui.renderer(comp_h, function()
    -- called every frame; must return an elem_handle
    return ftxui.vbox({ ... })
end)
```

### State queries (call from button callbacks or renderer)

```lua
str  = ftxui.get_input_value(h)       -- current text in an input
bool = ftxui.get_checkbox_state(h)    -- checked state
int  = ftxui.get_radiobox_selected(h) -- 0-based selected index
```

### Lifecycle

```lua
ftxui.set_root(comp_h)   -- REQUIRED: sets the root component
ftxui.quit()             -- signals the event loop to stop
```

---

## Building

**Requirements:** CMake ≥ 3.20, a C++20 compiler, internet access (FetchContent).

```bash
# 1. Clone / copy the project
git clone <this-repo> lua_ftxui && cd lua_ftxui

# 2. Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. Build  (first build downloads FTXUI + Lua — takes ~1 min)
cmake --build build -j$(nproc)

# 4. Run with the built-in demo script
./build/lua_ftxui
# or with a custom script:
./build/lua_ftxui path/to/my_ui.lua
```

---

## Extending the bridge

1. Add a new `static int l_my_widget(lua_State* L)` in `lua_ftxui.cpp`
2. Register it in the `ftxui_lib[]` table at the bottom of the file
3. Declare any new API in the Lua comment block at the top of `lua_ftxui.hpp`

No header changes are required unless you add new store fields.


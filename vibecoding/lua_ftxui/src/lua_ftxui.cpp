// lua_ftxui.cpp — Lua ↔ FTXUI bridge implementation (FTXUI v5.0+ FIXED & STABLE)
#include "lua_ftxui.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>
#include <vector>

namespace lua_ftxui {

// ── Registry key for the Store pointer ───────────────────────────────────────
static const char* STORE_KEY = "lua_ftxui_store";

static Store& get_store(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, STORE_KEY);
    Store* s = static_cast<Store*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!s) luaL_error(L, "lua_ftxui: store not initialised");
    return *s;
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static void push_handle(lua_State* L, Handle h) {
    lua_pushinteger(L, static_cast<lua_Integer>(h));
}

static Handle check_handle(lua_State* L, int idx) {
    return static_cast<Handle>(luaL_checkinteger(L, idx));
}

static std::vector<Handle> table_to_handles(lua_State* L, int idx) {
    luaL_checktype(L, idx, LUA_TTABLE);
    std::vector<Handle> out;
    lua_len(L, idx);
    int n = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    for (int i = 1; i <= n; ++i) {
        lua_geti(L, idx, i);
        out.push_back(static_cast<Handle>(luaL_checkinteger(L, -1)));
        lua_pop(L, 1);
    }
    return out;
}

// ── Callback helpers ──────────────────────────────────────────────────────────
static int store_callback(lua_State* L, int stack_idx) {
    luaL_checktype(L, stack_idx, LUA_TFUNCTION);
    lua_pushvalue(L, stack_idx);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

static void invoke_callback(lua_State* L, int ref) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        lua_pop(L, 1);
        lua_warning(L, "lua_ftxui callback error: ", 1);
        if (err) lua_warning(L, err, 0);
    }
}

// ── Element constructors ──────────────────────────────────────────────────────
static int l_text(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    auto& store = get_store(L);
    push_handle(L, store.add_element(ftxui::text(std::string(s))));
    return 1;
}

static int l_separator(lua_State* L) {
    auto& store = get_store(L);
    push_handle(L, store.add_element(ftxui::separator()));
    return 1;
}

static int l_hbox(lua_State* L) {
    auto& store   = get_store(L);
    auto  handles = table_to_handles(L, 1);
    ftxui::Elements elems;
    for (auto h : handles) elems.push_back(store.get_element(h));
    push_handle(L, store.add_element(ftxui::hbox(std::move(elems))));
    return 1;
}

static int l_vbox(lua_State* L) {
    auto& store   = get_store(L);
    auto  handles = table_to_handles(L, 1);
    ftxui::Elements elems;
    for (auto h : handles) elems.push_back(store.get_element(h));
    push_handle(L, store.add_element(ftxui::vbox(std::move(elems))));
    return 1;
}

static int l_border(lua_State* L) {
    auto& store = get_store(L);
    auto  h     = check_handle(L, 1);
    push_handle(L, store.add_element(ftxui::border(store.get_element(h))));
    return 1;
}

static int l_flex(lua_State* L) {
    auto& store = get_store(L);
    auto  h     = check_handle(L, 1);
    push_handle(L, store.add_element(store.get_element(h) | ftxui::flex));
    return 1;
}

// ── Component constructors ────────────────────────────────────────────────────

static int l_button(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    auto& store = get_store(L);

    int ref = store_callback(L, 2);

    auto btn = ftxui::Button(std::string(label), [L, ref]() {
        invoke_callback(L, ref);
    });

    push_handle(L, store.add_component({ std::move(btn) }));
    return 1;
}

static int l_input(lua_State* L) {
    const char* placeholder = luaL_optstring(L, 1, "");
    auto& store = get_store(L);

    auto state = std::make_shared<InputState>();
    ftxui::InputOption opts;
    opts.placeholder = placeholder;

    auto inp = ftxui::Input(&state->value, opts);

    ComponentEntry ce;
    ce.component   = std::move(inp);
    ce.input_state = std::move(state);
    push_handle(L, store.add_component(std::move(ce)));
    return 1;
}

static int l_checkbox(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    bool init = lua_toboolean(L, 2) != 0;
    auto& store = get_store(L);

    auto state     = std::make_shared<CheckboxState>();
    state->checked = init;

    ftxui::CheckboxOption opt = ftxui::CheckboxOption::Simple();
    auto cb = ftxui::Checkbox(label, &state->checked, opt);

    ComponentEntry ce;
    ce.component      = std::move(cb);
    ce.checkbox_state = std::move(state);
    push_handle(L, store.add_component(std::move(ce)));
    return 1;
}

static int l_radiobox(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    auto& store = get_store(L);

    auto entries = std::make_shared<std::vector<std::string>>();
    lua_len(L, 1);
    int n = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    for (int i = 1; i <= n; ++i) {
        lua_geti(L, 1, i);
        entries->push_back(luaL_checkstring(L, -1));
        lua_pop(L, 1);
    }

    auto state = std::make_shared<RadioboxState>();

    // Most stable for v5.0: use the (entries, selected, option) overload
    ftxui::RadioboxOption opt = ftxui::RadioboxOption::Simple();
    auto rb = ftxui::Radiobox(entries.get(), &state->selected, opt);

    ComponentEntry ce;
    ce.component      = std::move(rb);
    ce.radiobox_state = std::move(state);
    push_handle(L, store.add_component(std::move(ce)));
    return 1;
}

// ── Containers ────────────────────────────────────────────────────────────────
static int l_container_vert(lua_State* L) {
    auto& store   = get_store(L);
    auto  handles = table_to_handles(L, 1);
    ftxui::Components comps;
    for (auto h : handles) {
        auto* ce = store.get_component(h);
        if (!ce) luaL_error(L, "container_vert: handle %d is not a component", h);
        comps.push_back(ce->component);
    }
    auto container = ftxui::Container::Vertical(std::move(comps));
    push_handle(L, store.add_component({ std::move(container) }));
    return 1;
}

static int l_container_horiz(lua_State* L) {
    auto& store   = get_store(L);
    auto  handles = table_to_handles(L, 1);
    ftxui::Components comps;
    for (auto h : handles) {
        auto* ce = store.get_component(h);
        if (!ce) luaL_error(L, "container_horiz: handle %d is not a component", h);
        comps.push_back(ce->component);
    }
    auto container = ftxui::Container::Horizontal(std::move(comps));
    push_handle(L, store.add_component({ std::move(container) }));
    return 1;
}

// ── State queries ─────────────────────────────────────────────────────────────
static int l_get_input_value(lua_State* L) {
    auto& store = get_store(L);
    auto  h     = check_handle(L, 1);
    auto* ce    = store.get_component(h);
    if (!ce || !ce->input_state)
        return luaL_error(L, "get_input_value: handle %d is not an input", h);
    lua_pushstring(L, ce->input_state->value.c_str());
    return 1;
}

static int l_get_checkbox_state(lua_State* L) {
    auto& store = get_store(L);
    auto  h     = check_handle(L, 1);
    auto* ce    = store.get_component(h);
    if (!ce || !ce->checkbox_state)
        return luaL_error(L, "get_checkbox_state: handle %d is not a checkbox", h);
    lua_pushboolean(L, ce->checkbox_state->checked ? 1 : 0);
    return 1;
}

static int l_get_radiobox_selected(lua_State* L) {
    auto& store = get_store(L);
    auto  h     = check_handle(L, 1);
    auto* ce    = store.get_component(h);
    if (!ce || !ce->radiobox_state)
        return luaL_error(L, "get_radiobox_selected: handle %d is not a radiobox", h);
    lua_pushinteger(L, ce->radiobox_state->selected);
    return 1;
}

// ── Custom Renderer ───────────────────────────────────────────────────────────
static int l_renderer(lua_State* L) {
    auto& store    = get_store(L);
    Handle focus_h = check_handle(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    auto* ce = store.get_component(focus_h);
    if (!ce) return luaL_error(L, "renderer: handle %d is not a component", focus_h);

    int ref = store_callback(L, 2);
    ftxui::Component focused = ce->component;

    auto renderer = ftxui::Renderer(focused, [L, ref, &store]() -> ftxui::Element {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            lua_pop(L, 1);
            return ftxui::text("[render error] " + std::string(err ? err : "?"));
        }

        if (!lua_isinteger(L, -1)) {
            lua_pop(L, 1);
            return ftxui::text("[render_fn must return an elem_handle]");
        }

        Handle eh = static_cast<Handle>(lua_tointeger(L, -1));
        lua_pop(L, 1);
        return store.get_element(eh);
    });

    push_handle(L, store.add_component({ std::move(renderer) }));
    return 1;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
static int l_set_root(lua_State* L) {
    auto& store = get_store(L);
    auto  h     = check_handle(L, 1);
    auto* ce    = store.get_component(h);
    if (!ce) return luaL_error(L, "set_root: handle %d is not a component", h);
    store.root = ce->component;
    return 0;
}

static int l_quit(lua_State* L) {
    auto& store = get_store(L);
    store.quit_requested = true;
    return 0;
}

// ── Registration ──────────────────────────────────────────────────────────────
static const luaL_Reg ftxui_lib[] = {
    { "text",                  l_text                 },
    { "separator",             l_separator            },
    { "hbox",                  l_hbox                 },
    { "vbox",                  l_vbox                 },
    { "border",                l_border               },
    { "flex",                  l_flex                 },
    { "button",                l_button               },
    { "input",                 l_input                },
    { "checkbox",              l_checkbox             },
    { "radiobox",              l_radiobox             },
    { "container_vert",        l_container_vert       },
    { "container_horiz",       l_container_horiz      },
    { "renderer",              l_renderer             },
    { "get_input_value",       l_get_input_value      },
    { "get_checkbox_state",    l_get_checkbox_state   },
    { "get_radiobox_selected", l_get_radiobox_selected},
    { "set_root",              l_set_root             },
    { "quit",                  l_quit                 },
    { nullptr, nullptr }
};

void register_ftxui(lua_State* L, Store& store) {
    lua_pushlightuserdata(L, static_cast<void*>(&store));
    lua_setfield(L, LUA_REGISTRYINDEX, STORE_KEY);

    luaL_newlib(L, ftxui_lib);
    lua_setglobal(L, "ftxui");
}

} // namespace lua_ftxui

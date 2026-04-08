#include "bridge.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace neotui {

static Bridge& get_bridge(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, Bridge::STORE_KEY);
    Bridge* b = static_cast<Bridge*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!b) luaL_error(L, "neotui bridge: not initialised");
    return *b;
}

static void push_handle(lua_State* L, BridgeHandle h) {
    lua_pushinteger(L, static_cast<lua_Integer>(h));
}

static BridgeHandle check_handle(lua_State* L, int idx) {
    return static_cast<BridgeHandle>(luaL_checkinteger(L, idx));
}

static std::vector<BridgeHandle> table_to_handles(lua_State* L, int idx) {
    luaL_checktype(L, idx, LUA_TTABLE);
    std::vector<BridgeHandle> out;
    lua_len(L, idx);
    int n = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    for (int i = 1; i <= n; ++i) {
        lua_geti(L, idx, i);
        out.push_back(static_cast<BridgeHandle>(luaL_checkinteger(L, -1)));
        lua_pop(L, 1);
    }
    return out;
}

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
        lua_warning(L, "neotui bridge callback error: ", 1);
        if (err) lua_warning(L, err, 0);
    }
}

static int l_text(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    auto& bridge = get_bridge(L);
    push_handle(L, bridge.add_element(ftxui::text(std::string(s))));
    return 1;
}

static int l_separator(lua_State* L) {
    auto& bridge = get_bridge(L);
    push_handle(L, bridge.add_element(ftxui::separator()));
    return 1;
}

static int l_hbox(lua_State* L) {
    auto& bridge = get_bridge(L);
    auto handles = table_to_handles(L, 1);
    ftxui::Elements elems;
    for (auto h : handles) elems.push_back(bridge.get_element(h));
    push_handle(L, bridge.add_element(ftxui::hbox(std::move(elems))));
    return 1;
}

static int l_vbox(lua_State* L) {
    auto& bridge = get_bridge(L);
    auto handles = table_to_handles(L, 1);
    ftxui::Elements elems;
    for (auto h : handles) elems.push_back(bridge.get_element(h));
    push_handle(L, bridge.add_element(ftxui::vbox(std::move(elems))));
    return 1;
}

static int l_border(lua_State* L) {
    auto& bridge = get_bridge(L);
    auto h = check_handle(L, 1);
    push_handle(L, bridge.add_element(ftxui::border(bridge.get_element(h))));
    return 1;
}

static int l_flex(lua_State* L) {
    auto& bridge = get_bridge(L);
    auto h = check_handle(L, 1);
    push_handle(L, bridge.add_element(bridge.get_element(h) | ftxui::flex));
    return 1;
}

static int l_button(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    auto& bridge = get_bridge(L);

    int ref = store_callback(L, 2);

    auto btn = ftxui::Button(std::string(label), [L, ref]() {
        invoke_callback(L, ref);
    });

    push_handle(L, bridge.add_component({ std::move(btn) }));
    return 1;
}

static int l_input(lua_State* L) {
    const char* placeholder = luaL_optstring(L, 1, "");
    auto& bridge = get_bridge(L);

    auto state = std::make_shared<BridgeInputState>();
    ftxui::InputOption opts;
    opts.placeholder = placeholder;
    opts.multiline = false;

    auto inp = ftxui::Input(&state->value, opts);

    BridgeComponentEntry ce;
    ce.component = std::move(inp);
    ce.input_state = std::move(state);
    push_handle(L, bridge.add_component(std::move(ce)));
    return 1;
}

static int l_checkbox(lua_State* L) {
    const char* label = luaL_checkstring(L, 1);
    bool init = lua_toboolean(L, 2) != 0;
    auto& bridge = get_bridge(L);

    auto state = std::make_shared<BridgeCheckboxState>();
    state->checked = init;

    ftxui::CheckboxOption opt = ftxui::CheckboxOption::Simple();
    auto cb = ftxui::Checkbox(label, &state->checked, opt);

    BridgeComponentEntry ce;
    ce.component = std::move(cb);
    ce.checkbox_state = std::move(state);
    push_handle(L, bridge.add_component(std::move(ce)));
    return 1;
}

static int l_container_vert(lua_State* L) {
    auto& bridge = get_bridge(L);
    auto handles = table_to_handles(L, 1);
    ftxui::Components comps;
    for (auto h : handles) {
        auto* ce = bridge.get_component(h);
        if (!ce) luaL_error(L, "container_vert: handle %d is not a component", h);
        comps.push_back(ce->component);
    }
    auto container = ftxui::Container::Vertical(std::move(comps));
    push_handle(L, bridge.add_component({ std::move(container) }));
    return 1;
}

static int l_container_horiz(lua_State* L) {
    auto& bridge = get_bridge(L);
    auto handles = table_to_handles(L, 1);
    ftxui::Components comps;
    for (auto h : handles) {
        auto* ce = bridge.get_component(h);
        if (!ce) luaL_error(L, "container_horiz: handle %d is not a component", h);
        comps.push_back(ce->component);
    }
    auto container = ftxui::Container::Horizontal(std::move(comps));
    push_handle(L, bridge.add_component({ std::move(container) }));
    return 1;
}

static int l_get_input_value(lua_State* L) {
    auto& bridge = get_bridge(L);
    auto h = check_handle(L, 1);
    auto* ce = bridge.get_component(h);
    if (!ce || !ce->input_state)
        return luaL_error(L, "get_input_value: handle %d is not an input", h);
    lua_pushstring(L, ce->input_state->value.c_str());
    return 1;
}

static int l_get_checkbox_state(lua_State* L) {
    auto& bridge = get_bridge(L);
    auto h = check_handle(L, 1);
    auto* ce = bridge.get_component(h);
    if (!ce || !ce->checkbox_state)
        return luaL_error(L, "get_checkbox_state: handle %d is not a checkbox", h);
    lua_pushboolean(L, ce->checkbox_state->checked ? 1 : 0);
    return 1;
}

static int l_renderer(lua_State* L) {
    auto& bridge = get_bridge(L);
    BridgeHandle focus_h = check_handle(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    auto* ce = bridge.get_component(focus_h);
    if (!ce) return luaL_error(L, "renderer: handle %d is not a component", focus_h);

    int ref = store_callback(L, 2);
    ftxui::Component focused = ce->component;

    auto renderer = ftxui::Renderer(focused, [L, ref, &bridge]() -> ftxui::Element {
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

        BridgeHandle eh = static_cast<BridgeHandle>(lua_tointeger(L, -1));
        lua_pop(L, 1);
        return bridge.get_element(eh);
    });

    push_handle(L, bridge.add_component({ std::move(renderer) }));
    return 1;
}

static int l_set_root(lua_State* L) {
    auto& bridge = get_bridge(L);
    auto h = check_handle(L, 1);
    auto* ce = bridge.get_component(h);
    if (!ce) return luaL_error(L, "set_root: handle %d is not a component", h);
    bridge.set_workspace_component(ce->component);
    bridge.set_container(ce->component);
    return 0;
}

static int l_log(lua_State* L) {
    int n = lua_gettop(L);
    std::string output;
    for (int i = 1; i <= n; i++) {
        if (i > 1) output += "\t";
        output += luaL_tolstring(L, i, nullptr);
        lua_pop(L, 1);
    }
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm_buf;
    gmtime_r(&time, &tm_buf);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    
    std::cerr << "[workspace] " << time_buf << "." << std::setfill('0') 
              << std::setw(3) << ms.count() << " " << output << std::endl;
    
    auto& bridge = get_bridge(L);
    if (output.length() > 15) {
        output = output.substr(0, 12) + "...";
    }
    bridge.set_status(output);
    
    return 0;
}

static const luaL_Reg bridge_lib[] = {
    { "text",                  l_text                 },
    { "separator",             l_separator            },
    { "hbox",                  l_hbox                 },
    { "vbox",                  l_vbox                 },
    { "border",                l_border               },
    { "flex",                  l_flex                 },
    { "button",                l_button               },
    { "input",                 l_input                },
    { "checkbox",              l_checkbox             },
    { "container_vert",        l_container_vert       },
    { "container_horiz",       l_container_horiz      },
    { "renderer",              l_renderer             },
    { "get_input_value",       l_get_input_value      },
    { "get_checkbox_state",    l_get_checkbox_state   },
    { "set_root",              l_set_root             },
    { "log",                   l_log                  },
    { nullptr, nullptr }
};

void Bridge::register_in_lua(lua_State* L) {
    lua_pushlightuserdata(L, static_cast<void*>(this));
    lua_setfield(L, LUA_REGISTRYINDEX, STORE_KEY);

    luaL_newlib(L, bridge_lib);
    lua_setglobal(L, "workspace");
    
    lua_pushcfunction(L, l_log);
    lua_setglobal(L, "print");
    
    lua_pushcfunction(L, l_log);
    lua_setglobal(L, "log");
}

}
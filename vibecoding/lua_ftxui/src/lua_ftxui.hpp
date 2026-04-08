#pragma once
// lua_ftxui.hpp — Lua ↔ FTXUI bridge
//
// Design overview
// ───────────────
// The bridge exposes a global Lua table  `ftxui`  with functions that let a
// Lua script imperatively build an FTXUI component tree and wire up actions.
//
// All components are stored in a host-side ComponentStore indexed by integer
// handles.  Lua never holds C++ pointers; it only holds integer handles.
//
// Exposed Lua API
// ───────────────
//   -- Elements (pure DOM, no interaction)
//   ftxui.text(str)              → elem_handle
//   ftxui.separator()            → elem_handle
//   ftxui.hbox(t)               → elem_handle   -- t: table of elem_handles
//   ftxui.vbox(t)               → elem_handle
//   ftxui.border(elem_handle)   → elem_handle
//   ftxui.flex(elem_handle)     → elem_handle
//
//   -- Components (interactive)
//   ftxui.button(label, cb)     → comp_handle   -- cb: Lua function called on click
//   ftxui.input(placeholder)    → comp_handle
//   ftxui.checkbox(label, init) → comp_handle
//   ftxui.radiobox(entries)     → comp_handle   -- entries: table of strings
//   ftxui.container_vert(t)    → comp_handle   -- t: table of comp_handles
//   ftxui.container_horiz(t)   → comp_handle
//
//   -- Rendering helpers
//   ftxui.get_input_value(comp_handle)    → string
//   ftxui.get_checkbox_state(comp_handle) → bool
//   ftxui.get_radiobox_selected(comp_handle) → int (0-based)
//   ftxui.set_root(comp_handle)           -- sets the root component to render
//   ftxui.quit()                          -- signals the event loop to exit
//
//   -- Custom renderer: wraps a component with a Lua render function
//   ftxui.renderer(comp_handle, render_fn) → comp_handle
//     render_fn() must return an elem_handle built with ftxui.* element calls

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace lua_ftxui {

// ── Handle types ─────────────────────────────────────────────────────────────
using Handle = int;

// ── Store ─────────────────────────────────────────────────────────────────────
// Lives for the lifetime of the application; the same instance is shared
// between the bridge layer and main.cpp via a raw pointer stored in the
// Lua registry.

struct InputState {
    std::string value;
};
struct CheckboxState {
    bool checked{false};
};
struct RadioboxState {
    int selected{0};
};

struct ComponentEntry {
    ftxui::Component            component;
    // optional mutable state (for value queries from Lua)
    std::shared_ptr<InputState>    input_state;
    std::shared_ptr<CheckboxState> checkbox_state;
    std::shared_ptr<RadioboxState> radiobox_state;
};

class Store {
public:
    // ── Elements ──────────────────────────────────────────────────────────────
    Handle add_element(ftxui::Element e) {
        Handle h = next_handle_++;
        elements_[h] = std::move(e);
        return h;
    }

    ftxui::Element get_element(Handle h) const {
        auto it = elements_.find(h);
        if (it == elements_.end()) return ftxui::text("[invalid elem]");
        return it->second;
    }

    bool is_element(Handle h) const { return elements_.count(h) > 0; }

    // ── Components ────────────────────────────────────────────────────────────
    Handle add_component(ComponentEntry ce) {
        Handle h = next_handle_++;
        components_[h] = std::move(ce);
        return h;
    }

    ComponentEntry* get_component(Handle h) {
        auto it = components_.find(h);
        return it == components_.end() ? nullptr : &it->second;
    }

    bool is_component(Handle h) const { return components_.count(h) > 0; }

    // ── Root ─────────────────────────────────────────────────────────────────
    ftxui::Component root;
    bool             quit_requested{false};

private:
    Handle next_handle_{1};
    std::unordered_map<Handle, ftxui::Element>      elements_;
    std::unordered_map<Handle, ComponentEntry>      components_;
};

// ── Registration ──────────────────────────────────────────────────────────────
// Call once after lua_State is created.  `store` must outlive the lua_State.
void register_ftxui(lua_State* L, Store& store);

} // namespace lua_ftxui

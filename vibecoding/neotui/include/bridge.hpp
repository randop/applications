#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace neotui {

using BridgeHandle = int;

struct BridgeInputState {
    std::string value;
};
struct BridgeCheckboxState {
    bool checked{false};
};
struct BridgeRadioboxState {
    int selected{0};
};

struct BridgeComponentEntry {
    ftxui::Component component;
    std::shared_ptr<BridgeInputState> input_state;
    std::shared_ptr<BridgeCheckboxState> checkbox_state;
    std::shared_ptr<BridgeRadioboxState> radiobox_state;
};

class Bridge {
public:
    static constexpr const char* STORE_KEY = "neotui_bridge";

    BridgeHandle add_element(ftxui::Element e) {
        BridgeHandle h = next_handle_++;
        elements_[h] = std::move(e);
        return h;
    }

    ftxui::Element get_element(BridgeHandle h) const {
        auto it = elements_.find(h);
        if (it == elements_.end()) return ftxui::text("[invalid elem]");
        return it->second;
    }

    bool is_element(BridgeHandle h) const {
        return elements_.count(h) > 0;
    }

    BridgeHandle add_component(BridgeComponentEntry ce) {
        BridgeHandle h = next_handle_++;
        components_[h] = std::move(ce);
        return h;
    }

    BridgeComponentEntry* get_component(BridgeHandle h) {
        auto it = components_.find(h);
        return it == components_.end() ? nullptr : &it->second;
    }

    bool is_component(BridgeHandle h) const {
        return components_.count(h) > 0;
    }

    void set_workspace_component(ftxui::Component comp) {
        workspace_component_ = std::move(comp);
    }

    ftxui::Component get_workspace_component() const {
        return workspace_component_;
    }

    void register_in_lua(lua_State* L);

private:
    BridgeHandle next_handle_{1};
    std::unordered_map<BridgeHandle, ftxui::Element> elements_;
    std::unordered_map<BridgeHandle, BridgeComponentEntry> components_;
    ftxui::Component workspace_component_;
};

}
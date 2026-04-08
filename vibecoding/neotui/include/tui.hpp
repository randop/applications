#pragma once

#ifndef NEO_TUI_H
#define NEO_TUI_H

#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <memory>

#include "scroller.hpp"

namespace neotui {

struct ThemeColors {
    ftxui::Color background;
    ftxui::Color foreground;
    ftxui::Color accent;
    ftxui::Color border;
    ftxui::Color statusbar;
    ftxui::Color fg_gutter;
    ftxui::Color bg_highlight;
    ftxui::Color comment;
    ftxui::Color green;
    ftxui::Color yellow;
    ftxui::Color red;
    ftxui::Color magenta;
    ftxui::Color purple;
    ftxui::Color cyan;
    ftxui::Color orange;
    std::string name;
};

class TUI {
public:
    TUI();
    ~TUI();

    void setTheme(const ThemeColors& theme);
    void setCode(const std::string& code);
    void setCodeOutput(const std::string& output);
    void setWorkspaceOutput(const std::string& output);
    void setFirstname(const std::string& name);
    void setLastname(const std::string& name);
    void setConfigStatus(bool loaded);
    void setOnRun(std::function<void()> callback);
    void setOnProcess(std::function<void()> callback);
    void run();

    std::string getCode() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace neotui

#endif // NEO_TUI_H

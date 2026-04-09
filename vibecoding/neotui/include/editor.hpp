#pragma once

#ifndef NEO_EDITOR_H
#define NEO_EDITOR_H

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace neotui {

class Editor {
public:
    Editor();
    ~Editor() = default;

    // Set the content of the editor
    void setContent(const std::string& content);
    std::string getContent() const;

    // Get the FTXUI component
    ftxui::Component getComponent();

private:
    struct Line {
        std::string text;
        int number;
    };

    std::vector<Line> lines_;
    int current_line_ = 0;
    int current_col_ = 0;
    int top_line_ = 0;  // First visible line
    bool show_line_numbers_ = true;

    // FTXUI components
    ftxui::Component input_component_;
    ftxui::Component button_component_;

    // Modal state
    bool show_modal_;

    // Helper methods
    void updateLinesFromContent();
    void updateContentFromLines();
    void updateViewport();
    ftxui::Element renderLine(int line_idx, int display_idx);
};

} // namespace neotui

#endif // NEO_EDITOR_H
#include "editor.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace neotui {

Editor::Editor() : show_modal_(false) {
    std::cerr << "[DEBUG] Editor constructor called" << std::endl;

    // Create button component
    button_component_ = ftxui::Button("Panel 4 Editor Button", [this]() {
        std::cerr << "[DEBUG] Button callback executed!" << std::endl;
        show_modal_ = !show_modal_;
        std::cerr << "[DEBUG] show_modal_ toggled to: " << (show_modal_ ? "true" : "false") << std::endl;
    });

    std::cerr << "[DEBUG] Button component created" << std::endl;

    // Create container with button
    input_component_ = ftxui::Container::Vertical({
        button_component_
    });

    std::cerr << "[DEBUG] Container created" << std::endl;
}

void Editor::setContent(const std::string& content) {
    // Simplified for button testing
}

std::string Editor::getContent() const {
    return "Panel 4 Editor (Button Test)";
}

ftxui::Component Editor::getComponent() {
    return input_component_;
}

// Simplified implementations for button testing
void Editor::updateLinesFromContent() {
    // Not used in button test
}

void Editor::updateContentFromLines() {
    // Not used in button test
}

// Not used in button test
ftxui::Element Editor::renderLine(int line_idx, int display_idx) {
    return ftxui::text("");  // Placeholder
}

// Not used in button test
void Editor::updateViewport() {
    // Placeholder
}

} // namespace neotui
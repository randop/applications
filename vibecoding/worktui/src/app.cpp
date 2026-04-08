#include "app.hpp"
#include "text_editor.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

App::App() {
  // Panel 0: Text Editor
  panels_.push_back(std::make_shared<TextEditor>());
  // Panels 1-3: Placeholders
  for (int i = 1; i < 4; ++i) {
    panels_.push_back(ftxui::Renderer([i] {
      return ftxui::text("Placeholder for panel " + std::to_string(i + 1)) | ftxui::flex;
    }));
  }
  modal_container_ = ftxui::Container::Vertical({});
  for (int i = 0; i < 4; ++i) {
    modal_container_->Add(ftxui::Button("Panel " + std::to_string(i + 1), [this, i] {
      current_panel_ = i;
      show_modal_ = false;
    }));
  }
}

ftxui::Element App::OnRender() {
  auto current = panels_[current_panel_];
  auto status = ftxui::text("Status: Panel " + std::to_string(current_panel_ + 1));
  auto current_element = current->Render() | ftxui::yflex;
  auto main_content = ftxui::vbox(current_element, ftxui::separator(), status);
  if (show_modal_) {
    auto main_component = ftxui::Renderer([main_content] { return main_content; });
    auto modal_component = ftxui::Renderer([this] { return ftxui::window(ftxui::text("Select Panel"), modal_container_->Render()); });
    return ftxui::Modal(main_component, modal_component, &show_modal_)->Render();
  } else {
    return main_content;
  }
}

bool App::OnEvent(ftxui::Event event) {
  if (show_modal_) {
    return modal_container_->OnEvent(event);
  } else {
    if (event == ftxui::Event::CtrlP) {
      show_modal_ = true;
      return true;
    }
    return panels_[current_panel_]->OnEvent(event);
  }
}
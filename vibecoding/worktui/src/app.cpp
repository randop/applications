#include "app.hpp"
#include "text_editor.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

class TextEditorPanel : public ftxui::ComponentBase {
public:
  TextEditorPanel();
  ftxui::Element OnRender() override;
  bool OnEvent(ftxui::Event event) override;

private:
  int selected_ = 0;
  int focused_index_ = 3;  // start focus on editor
  ftxui::Component dropdown_;
  ftxui::Component input_;
  ftxui::Component button_;
  ftxui::Component editor_component_;
  ftxui::Component container_;
};

TextEditorPanel::TextEditorPanel() {
  dropdown_ = ftxui::Dropdown(std::vector<std::string>{"GET", "POST", "PUT", "PATCH", "DELETE"}, &selected_);
  input_ = ftxui::Input();
  button_ = ftxui::Button("DO", [] { /* TODO: implement action */ });
  editor_component_ = std::make_shared<TextEditor>();
  container_ = ftxui::Container::Vertical({dropdown_, input_, button_, editor_component_}, &focused_index_);
}

ftxui::Element TextEditorPanel::OnRender() {
  return container_->Render() | ftxui::yflex;
}

bool TextEditorPanel::OnEvent(ftxui::Event event) {
  return container_->OnEvent(event);
}

App::App() {
  // Panel 0: UI elements + Text Editor
  panels_.push_back(std::make_shared<TextEditorPanel>());
  // Panel 1: Text Editor instance 2
  panels_.push_back(std::make_shared<TextEditor>());
  // Panels 2-3: Placeholders
  for (int i = 2; i < 4; ++i) {
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
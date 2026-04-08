#ifndef APP_HPP
#define APP_HPP

#include <ftxui/component/component_base.hpp>
#include <memory>
#include <vector>

class TextEditor;

class App : public ftxui::ComponentBase {
public:
  App();
  ftxui::Element OnRender() override;
  bool OnEvent(ftxui::Event event) override;

private:
  std::vector<ftxui::Component> panels_;
  int current_panel_ = 0;
  bool show_modal_ = false;
  ftxui::Component modal_container_;
};

#endif // APP_HPP
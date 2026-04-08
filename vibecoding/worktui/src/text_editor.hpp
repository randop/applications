#ifndef TEXT_EDITOR_HPP
#define TEXT_EDITOR_HPP

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/animation.hpp>
#include <ftxui/screen/terminal.hpp>
#include <string>
#include <vector>

class TextEditor : public ftxui::ComponentBase {
public:
  TextEditor();
  ftxui::Element OnRender() override;
  bool OnEvent(ftxui::Event event) override;
  void OnAnimation(ftxui::animation::Params& params) override;

private:
  std::vector<std::string> lines_;
  int cursor_line_ = 0;
  int cursor_col_ = 0;
  int scroll_y_ = 0;
  int max_visible_lines_ = 20;
  bool cursor_visible_ = true;
  float animation_time_ = 0.0f;
  void adjust_scroll();
  std::vector<std::string> wrap_text(const std::string& text, int width);
};

#endif // TEXT_EDITOR_HPP
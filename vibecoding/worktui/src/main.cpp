#include <ftxui/component/screen_interactive.hpp>
#include <memory>
#include "text_editor.hpp"

int main() {
  auto screen = ftxui::ScreenInteractive::Fullscreen();
  auto editor = std::make_shared<TextEditor>();
  screen.Loop(editor);
  return 0;
}
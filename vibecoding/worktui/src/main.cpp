#include <ftxui/component/screen_interactive.hpp>
#include <memory>
#include "app.hpp"

int main() {
  auto screen = ftxui::ScreenInteractive::Fullscreen();
  auto app = std::make_shared<App>();
  screen.Loop(app);
  return 0;
}
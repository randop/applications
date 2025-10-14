#include <cstdlib>
#include <memory>
#include <string>

#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

int main() {
  using namespace ftxui;

  std::string content_1;
  std::string content_2;
  auto textarea_1 = Input(&content_1);
  auto textarea_2 = Input(&content_2);
  int size = 50;
  auto layout = ResizableSplitLeft(textarea_1, textarea_2, &size);

  auto component = Renderer(layout, [&] {
    return vbox({
               text("Input:"),
               separator(),
               layout->Render() | flex,
           }) |
           border;
  });

  auto screen = ScreenInteractive::Fullscreen();
  screen.Loop(component);

  return EXIT_SUCCESS;
}

#include <cstdlib>
#include <memory>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/flexbox_config.hpp>
#include <ftxui/screen/color.hpp>

#include "tab_components.hpp"

using namespace ftxui;

int main() {
  std::vector<std::string> tab_values{
      "AI", "Workflows", "Kanban", "Tools", "Notifications",
  };
  int tab_selected = 0;
  auto tab_toggle = Toggle(&tab_values, &tab_selected);

  int window_1_left = 20;
  int window_1_top = 10;
  int window_1_width = 40;
  int window_1_height = 20;

  auto ai_tab = AITab();
  auto workflows_tab = Window({
      .inner = WorkflowsTab(),
      .title = "First window",
      .left = &window_1_left,
      .top = &window_1_top,
      .width = &window_1_width,
      .height = &window_1_height,
  });
  auto kanban_tab = Window({
      .inner = KanbanTab(),
      .title = "First window",
      .left = &window_1_left,
      .top = &window_1_top,
      .width = &window_1_width,
      .height = &window_1_height,
  });
  auto tools_tab = ToolsTab();
  auto notifications_tab = NotificationsTab();

  auto tab_container = Container::Tab(
      {ai_tab, workflows_tab, kanban_tab, tools_tab, notifications_tab},
      &tab_selected);

  auto container = Container::Vertical({
      tab_toggle,
      tab_container | flex,
  });

  auto renderer = Renderer(container, [&] {
    return vbox({
               tab_toggle->Render(),
               separator(),
               tab_container->Render(),
           }) |
           border;
  });

  auto screen = ScreenInteractive::Fullscreen();
  screen.Loop(renderer);

  return EXIT_SUCCESS;
}

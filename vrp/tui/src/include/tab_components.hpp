#pragma once

#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/flexbox_config.hpp>
#include <ftxui/screen/color.hpp>

using namespace ftxui;

Element ColorTile(int red, int green, int blue) {
  return text("") | size(WIDTH, GREATER_THAN, 14) |
         size(HEIGHT, GREATER_THAN, 7) | bgcolor(Color::RGB(red, green, blue));
}

Element ColorString(int red, int green, int blue) {
  return text("RGB = (" +                   //
              std::to_string(red) + "," +   //
              std::to_string(green) + "," + //
              std::to_string(blue) + ")"    //
  );
}

Component DummyWindowContent() {
  class Impl : public ComponentBase {
  private:
    float scroll_x = 0.1;
    float scroll_y = 0.1;

  public:
    Impl() {
      auto content = Renderer([=] {
        const std::string lorem =
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed "
            "do eiusmod tempor incididunt ut labore et dolore magna "
            "aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
            "ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis "
            "aute irure dolor in reprehenderit in voluptate velit esse "
            "cillum dolore eu fugiat nulla pariatur. Excepteur sint "
            "occaecat cupidatat non proident, sunt in culpa qui officia "
            "deserunt mollit anim id est laborum.";
        return vbox({
            text(lorem.substr(0, -1)),   text(lorem.substr(5, -1)),   text(""),
            text(lorem.substr(10, -1)),  text(lorem.substr(15, -1)),  text(""),
            text(lorem.substr(20, -1)),  text(lorem.substr(25, -1)),  text(""),
            text(lorem.substr(30, -1)),  text(lorem.substr(35, -1)),  text(""),
            text(lorem.substr(40, -1)),  text(lorem.substr(45, -1)),  text(""),
            text(lorem.substr(50, -1)),  text(lorem.substr(55, -1)),  text(""),
            text(lorem.substr(60, -1)),  text(lorem.substr(65, -1)),  text(""),
            text(lorem.substr(70, -1)),  text(lorem.substr(75, -1)),  text(""),
            text(lorem.substr(80, -1)),  text(lorem.substr(85, -1)),  text(""),
            text(lorem.substr(90, -1)),  text(lorem.substr(95, -1)),  text(""),
            text(lorem.substr(100, -1)), text(lorem.substr(105, -1)), text(""),
            text(lorem.substr(110, -1)), text(lorem.substr(115, -1)), text(""),
            text(lorem.substr(120, -1)), text(lorem.substr(125, -1)), text(""),
            text(lorem.substr(130, -1)), text(lorem.substr(135, -1)), text(""),
            text(lorem.substr(140, -1)),
        });
      });

      auto scrollable_content = Renderer(content, [&, content] {
        return content->Render() | focusPositionRelative(scroll_x, scroll_y) |
               frame | flex;
      });

      SliderOption<float> option_x;
      option_x.value = &scroll_x;
      option_x.min = 0.f;
      option_x.max = 1.f;
      option_x.increment = 0.1f;
      option_x.direction = Direction::Right;
      option_x.color_active = Color::Blue;
      option_x.color_inactive = Color::BlueLight;
      auto scrollbar_x = Slider(option_x);

      SliderOption<float> option_y;
      option_y.value = &scroll_y;
      option_y.min = 0.f;
      option_y.max = 1.f;
      option_y.increment = 0.1f;
      option_y.direction = Direction::Down;
      option_y.color_active = Color::Yellow;
      option_y.color_inactive = Color::YellowLight;
      auto scrollbar_y = Slider(option_y);

      Add(Container::Vertical({
          Container::Horizontal({
              scrollable_content,
              scrollbar_y,
          }) | flex,
          Container::Horizontal({
              scrollbar_x,
              Renderer([] { return text(L"x"); }),
          }),
      }));
    }
  };
  return Make<Impl>();
}

Component AITab() {
  class Impl : public ComponentBase {
  private:
    std::string content_1;
    std::string content_2;
    int size = 50;
    bool checked[3] = {false, false, false};
    float slider = 50;

  public:
    Impl() {
      auto textarea_1 = Input(&content_1);
      auto textarea_2 = Input(&content_2);
      auto layout = ResizableSplitLeft(textarea_1, textarea_2, &size);

      Add(Container::Vertical({
              textarea_1,
              textarea_2,
              Checkbox("Check me", &checked[0]),
              Checkbox("Check me", &checked[1]),
              Checkbox("Check me", &checked[2]),
              Slider("Slider", &slider, 0.f, 100.f),
          }) |
          flex);
    }
  };
  return Make<Impl>();
}

Component WorkflowsTab() {
  class Impl : public ComponentBase {

  public:
    Impl() {
      auto textComponent = Renderer([&] { return text("WorkflowsTab: "); });

      Add(Container::Vertical({textComponent}));
    }
  };
  return Make<Impl>();
}

Component KanbanTab() {
  class Impl : public ComponentBase {

  public:
    Impl() {
      auto textComponent = Renderer([&] { return text("KanbanTab: "); });

      Add(Container::Vertical({textComponent}));
    }
  };
  return Make<Impl>();
}

Component ToolsTab() {
  class Impl : public ComponentBase {
  private:
    int red = 128;
    int green = 25;
    int blue = 100;
    int value = 50;

  public:
    Impl() {
      auto slider = Slider("Value:", &value, 0, 100, 1);

      auto slider_red = Slider("Red  :", &red, 0, 255, 1);
      auto slider_green = Slider("Green:", &green, 0, 255, 1);
      auto slider_blue = Slider("Blue :", &blue, 0, 255, 1);

      auto sliderColorString =
          Renderer([&] { return ColorString(red, green, blue); });

      auto sliderColorTitle =
          Renderer([&] { return ColorTile(red, green, blue); });

      Add(Container::Vertical({slider, sliderColorTitle, sliderColorString,
                               slider_red, slider_green, slider_blue}));
    }
  };
  return Make<Impl>();
}

Component NotificationsTab() {
  class Impl : public ComponentBase {
  private:
    int computedValue = 0;

  public:
    Impl() {
      auto action = [&] { computedValue++; };
      auto action_renderer = Renderer(
          [&] { return text("count = " + std::to_string(computedValue)); });

      auto buttons =
          Container::Vertical({
              action_renderer,
              Renderer([] { return separator(); }),
              Container::Horizontal({
                  Container::Vertical({
                      Button("Ascii 1", action, ButtonOption::Ascii()),
                      Button("Ascii 2", action, ButtonOption::Ascii()),
                      Button("Ascii 3", action, ButtonOption::Ascii()),
                  }),
                  Renderer([] { return separator(); }),
                  Container::Vertical({
                      Button("Simple 1", action, ButtonOption::Simple()),
                      Button("Simple 2", action, ButtonOption::Simple()),
                      Button("Simple 3", action, ButtonOption::Simple()),
                  }),
                  Renderer([] { return separator(); }),
                  Container::Vertical({
                      Button("Animated 1", action, ButtonOption::Animated()),
                      Button("Animated 2", action, ButtonOption::Animated()),
                      Button("Animated 3", action, ButtonOption::Animated()),
                  }),
                  Renderer([] { return separator(); }),
                  Container::Vertical({
                      Button("Animated 4", action,
                             ButtonOption::Animated(Color::Red)),
                      Button("Animated 5", action,
                             ButtonOption::Animated(Color::Green)),
                      Button("Animated 6", action,
                             ButtonOption::Animated(Color::Blue)),
                  }),
              }),
          }) |
          border;

      Add(buttons);
    }
  };
  return Make<Impl>();
}

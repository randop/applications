#include "tui.hpp"
#include "helpers.hpp"
#include <iostream>
#include <chrono>
#include <ctime>
#include <ftxui/screen/terminal.hpp>

namespace neotui {

struct TUI::Impl {
    ThemeColors theme;
    std::string code;
    std::string code_output;
    std::string workspace_output;
    std::string firstname;
    std::string lastname;
    bool config_loaded = false;
    int active_panel = 0;
    Bridge* bridge = nullptr;
    
    ftxui::Component code_input;
    ftxui::Component run_button;
    ftxui::Component process_button;
    ftxui::Component workspace_renderer;
    ftxui::Component code_output_renderer;
    ftxui::Component firstname_input;
    ftxui::Component lastname_input;
    ftxui::Component panel1;
    ftxui::Component panel2;
    ftxui::Component panel3;
    
    std::function<void()> on_run;
    std::function<void()> on_process;
    std::function<void(int)> on_panel_change;
    
    Impl() {
        theme = {
            ftxui::Color::RGB(34, 36, 54),
            ftxui::Color::RGB(200, 211, 245),
            ftxui::Color::RGB(130, 170, 255),
            ftxui::Color::RGB(59, 66, 97),
            ftxui::Color::RGB(47, 51, 77),
            ftxui::Color::RGB(59, 66, 97),
            ftxui::Color::RGB(47, 51, 77),
            ftxui::Color::RGB(99, 109, 166),
            ftxui::Color::RGB(195, 232, 141),
            ftxui::Color::RGB(255, 199, 119),
            ftxui::Color::RGB(255, 117, 127),
            ftxui::Color::RGB(192, 153, 255),
            ftxui::Color::RGB(252, 167, 234),
            ftxui::Color::RGB(134, 225, 252),
            ftxui::Color::RGB(255, 150, 108),
            "Moon"
        };
    }
};

TUI::TUI() : pImpl(std::make_unique<Impl>()) {}

TUI::~TUI() = default;

void TUI::setTheme(const ThemeColors& theme) {
    pImpl->theme = theme;
}

void TUI::setCode(const std::string& code) {
    pImpl->code = code;
}

void TUI::setCodeOutput(const std::string& output) {
    pImpl->code_output = output;
}

void TUI::setWorkspaceOutput(const std::string& output) {
    pImpl->workspace_output = output;
}

void TUI::setFirstname(const std::string& name) {
    pImpl->firstname = name;
}

void TUI::setLastname(const std::string& name) {
    pImpl->lastname = name;
}

void TUI::setConfigStatus(bool loaded) {
    pImpl->config_loaded = loaded;
}

void TUI::setOnRun(std::function<void()> callback) {
    pImpl->on_run = callback;
}

void TUI::setOnProcess(std::function<void()> callback) {
    pImpl->on_process = callback;
}

void TUI::setBridge(Bridge* bridge) {
    pImpl->bridge = bridge;
}

void TUI::setOnPanelChange(std::function<void(int)> callback) {
    pImpl->on_panel_change = callback;
}

std::string TUI::getCode() const {
    return pImpl->code;
}

void TUI::run() {
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    
    auto& impl = *pImpl;
    auto& theme = impl.theme;
    
    auto input_option = ftxui::InputOption::Default();
    input_option.transform = [&theme](ftxui::InputState state) {
        auto bg = theme.background;
        auto fg = state.is_placeholder ? theme.comment : theme.foreground;
        auto focus_fg = theme.yellow;
        auto fg_color = state.focused ? focus_fg : fg;
        auto border_fg = state.focused ? theme.yellow : theme.border;
        
        auto element = state.element | ftxui::bgcolor(bg) | ftxui::color(fg_color);
        if (state.focused) {
            element = element | ftxui::borderStyled(ftxui::BorderStyle::ROUNDED, border_fg);
        }
        return element;
    };
    
    auto firstname_option = ftxui::InputOption::Default();
    firstname_option.multiline = false;
    firstname_option.transform = input_option.transform;
    impl.firstname_input = ftxui::Input(&impl.firstname, "Firstname", firstname_option);
    
    auto lastname_option = ftxui::InputOption::Default();
    lastname_option.multiline = false;
    lastname_option.transform = input_option.transform;
    impl.lastname_input = ftxui::Input(&impl.lastname, "Lastname", lastname_option);
    
    auto code_option = ftxui::InputOption::Default();
    code_option.transform = input_option.transform;
    impl.code_input = ftxui::Input(&impl.code, "LUA", code_option);
    
    impl.run_button = ftxui::Button("Run", [&impl]() {
        if (impl.on_run) impl.on_run();
    });
    
    impl.process_button = ftxui::Button("Process", [&impl, &screen]() {
        if (impl.on_process) {
          impl.on_process();
        }
        screen.PostEvent(ftxui::Event::Custom);
    });

    // Create the scrollable component
    auto content_renderer = ftxui::Renderer([&]() {
        return ftxui::paragraph(impl.workspace_output) | ftxui::bgcolor(theme.background) 
            | ftxui::color(theme.foreground);      
    });

    impl.workspace_renderer = ftxui::Renderer([&]() {
      return content_renderer->Render();
    });

    impl.workspace_renderer |= ftxui::Scroller;
    
    impl.code_output_renderer = ftxui::Renderer([&]() {
        return ftxui::text(impl.code_output) 
            | ftxui::bgcolor(theme.background) 
            | ftxui::color(theme.foreground)
            | ftxui::borderStyled(ftxui::BorderStyle::ROUNDED, theme.border);
    });
    
    auto container = ftxui::Container::Vertical({
        impl.workspace_renderer,
        impl.firstname_input,
        impl.lastname_input,
        impl.code_input,
        impl.run_button,
        impl.process_button,
        impl.code_output_renderer
    });
    
    impl.panel1 = ftxui::Renderer(container, [&]() {
        return ftxui::vbox({
            impl.workspace_renderer->Render() | ftxui::yflex,
            ftxui::separator() | ftxui::color(theme.border),
            ftxui::hbox({
                ftxui::text("Firstname: ") | ftxui::color(theme.accent),
                impl.firstname_input->Render() | ftxui::bgcolor(theme.background) | ftxui::color(theme.foreground) | ftxui::xflex
            }),
            ftxui::hbox({
                ftxui::text("Lastname: ") | ftxui::color(theme.accent),
                impl.lastname_input->Render() | ftxui::bgcolor(theme.background) | ftxui::color(theme.foreground) | ftxui::xflex
            }),
            ftxui::separator() | ftxui::color(theme.border),
            ftxui::text(":") | ftxui::color(theme.accent),
            ftxui::hbox({
                impl.code_input->Render() | ftxui::bgcolor(theme.background) | ftxui::color(theme.foreground) | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 4),
                impl.run_button->Render() | ftxui::bgcolor(theme.background) | ftxui::color(theme.foreground),
                impl.process_button->Render() | ftxui::bgcolor(theme.background) | ftxui::color(theme.foreground)
            }),
            impl.code_output_renderer->Render(),
        });
    });

    impl.panel2 = ftxui::Renderer([&]() {
        std::string labels = "Panel 2: ";
        if (impl.bridge) {
            labels.append("bridge,");
            auto ws = impl.bridge->get_workspace_component();
            if (ws) {
              labels.append("wscomponents,");
                return ws->Render() | ftxui::bgcolor(theme.background) | ftxui::color(theme.foreground);
            }
        }
        return ftxui::vbox({
            ftxui::text(labels) | ftxui::color(theme.accent) | ftxui::center,
            ftxui::filler(),
        }) | ftxui::bgcolor(theme.background) | ftxui::color(theme.foreground);
    });

    impl.panel3 = ftxui::Renderer([&]() {
        return ftxui::vbox({
            ftxui::text("Panel 3: Settings") | ftxui::color(theme.accent) | ftxui::center,
            ftxui::filler(),
        }) | ftxui::bgcolor(theme.background) | ftxui::color(theme.foreground);
    });

    auto renderer = ftxui::Renderer(container, [&]() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::string time_str = std::ctime(&t);
        time_str.erase(time_str.find_last_not_of(" \n\r\t") + 1);
        
        std::string config_status = impl.config_loaded ? "OK" : "Failed";
        std::string panel_label = impl.active_panel == 0 ? "Main" : (impl.active_panel == 1 ? "Workspace" : "Settings");
        std::string left_status = " NORMAL  lua  NeoTUI  Panel: " + panel_label + "  Theme: " + theme.name + "  Config: " + config_status;

        ftxui::Element current_panel;
        if (impl.active_panel == 0) {
            current_panel = impl.panel1->Render() | ftxui::yflex | ftxui::xflex;
        } else if (impl.active_panel == 1) {
            current_panel = impl.panel2->Render() | ftxui::yflex | ftxui::xflex;
        } else {
            current_panel = impl.panel3->Render() | ftxui::yflex | ftxui::xflex;
        }

        return ftxui::vbox({
            current_panel,
            ftxui::separator() | ftxui::color(theme.border),
            ftxui::hbox({
                ftxui::text(left_status) | ftxui::color(theme.foreground) | ftxui::bgcolor(theme.statusbar),
                ftxui::filler() | ftxui::bgcolor(theme.statusbar),
                ftxui::text(time_str) | ftxui::color(theme.foreground) | ftxui::bgcolor(theme.statusbar)
            }) | ftxui::bgcolor(theme.statusbar)
        }) | ftxui::bgcolor(theme.background) | ftxui::color(theme.foreground) | ftxui::yflex;
    });

    renderer |= ftxui::CatchEvent([&](const ftxui::Event& event) {
        std::string repr = event.input();
        char alt1[] = {27, '1', 0};
        char alt2[] = {27, '2', 0};
        char alt3[] = {27, '3', 0};
        if (repr == alt1) {
            impl.active_panel = 0;
            screen.PostEvent(ftxui::Event::Custom);
            return true;
        }
        if (repr == alt2) {
            impl.active_panel = 1;
            if (impl.on_panel_change) {
                impl.on_panel_change(1);
            }
            screen.PostEvent(ftxui::Event::Custom);
            return true;
        }
        if (repr == alt3) {
            impl.active_panel = 2;
            screen.PostEvent(ftxui::Event::Custom);
            return true;
        }
        return false;
    });

    screen.Loop(renderer);

}

}

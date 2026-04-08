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
    
    ftxui::Component code_input;
    ftxui::Component run_button;
    ftxui::Component process_button;
    ftxui::Component workspace_renderer;
    ftxui::Component code_output_renderer;
    ftxui::Component firstname_input;
    ftxui::Component lastname_input;
    
    std::function<void()> on_run;
    std::function<void()> on_process;
    
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
        std::vector<std::string> lines = split_lines(impl.workspace_output);
        std::vector<ftxui::Element> elements;
        for (const auto& line : lines) {
            elements.push_back(ftxui::text(line));
        }
        return ftxui::vbox(std::move(elements));
        /*
        return ftxui::paragraph(impl.workspace_output)
             | ftxui::borderStyled(ftxui::ROUNDED, ftxui::Color::Blue) | ftxui::bgcolor(theme.background) 
            | ftxui::color(theme.foreground);
        */
    });

    impl.workspace_renderer = ftxui::Renderer([&]() {
      return content_renderer->Render()
         | ftxui::frame                    // this makes it scrollable
         | ftxui::borderStyled(ftxui::ROUNDED, ftxui::Color::Blue);
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
    
    auto renderer = ftxui::Renderer(container, [&]() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::string time_str = std::ctime(&t);
        time_str.erase(time_str.find_last_not_of(" \n\r\t") + 1);
        
        std::string config_status = impl.config_loaded ? "OK" : "Failed";
        std::string left_status = " NORMAL  lua  NeoTUI  Theme: " + theme.name + "  Config: " + config_status;

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
            ftxui::separator() | ftxui::color(theme.border),
            ftxui::hbox({
                ftxui::text(left_status) | ftxui::color(theme.foreground) | ftxui::bgcolor(theme.statusbar),
                ftxui::filler() | ftxui::bgcolor(theme.statusbar),
                ftxui::text(time_str) | ftxui::color(theme.foreground) | ftxui::bgcolor(theme.statusbar)
            }) | ftxui::bgcolor(theme.statusbar)
        }) | ftxui::bgcolor(theme.background) | ftxui::color(theme.foreground) | ftxui::yflex;
    });

    screen.Loop(renderer);

}

}

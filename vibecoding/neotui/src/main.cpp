#include <lua.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>

int main() {
    // Initialize Lua state like Neovim does
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    // Initialize FTXUI screen
    auto screen = ftxui::ScreenInteractive::TerminalOutput();
    std::string code;
    std::string output;

    // Input component for Lua code
    auto input_component = ftxui::Input(&code, "Enter Lua code here...");

    // Button to run the code
    auto run_button = ftxui::Button("Run Lua Code", [&]() {
        if (luaL_dostring(L, code.c_str()) == LUA_OK) {
            // If successful, check for return value
            if (lua_gettop(L) > 0) {
                output = lua_tostring(L, -1);
                lua_pop(L, 1);
            } else {
                output = "Code executed successfully.";
            }
        } else {
            // If error, get error message
            output = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
    });

    // Display output
    auto output_renderer = ftxui::Renderer([&]() {
        return ftxui::text(output) | ftxui::border;
    });

    // Layout: vertical container with input, button, output
    auto container = ftxui::Container::Vertical({
        input_component,
        run_button,
        output_renderer
    });

    // Main renderer
    auto renderer = ftxui::Renderer(container, [&]() {
        return ftxui::vbox({
            ftxui::text("NeoTUI - Lua Interpreter") | ftxui::bold | ftxui::center,
            ftxui::separator(),
            input_component->Render(),
            run_button->Render(),
            output_renderer->Render()
        }) | ftxui::border;
    });

    // Run the loop
    screen.Loop(renderer);

    // Clean up Lua state
    lua_close(L);
    return 0;
}
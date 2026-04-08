cmake_minimum_required(VERSION 3.20)
project(lua_ftxui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# ─── FTXUI ────────────────────────────────────────────────────────────────────
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG        v5.0.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ftxui)

# Find Lua
find_package(Lua REQUIRED)

# Create an imported target (modern CMake style)
if(Lua_FOUND AND NOT TARGET Lua::Lua)
    add_library(Lua::Lua INTERFACE IMPORTED)
    set_target_properties(Lua::Lua PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${LUA_LIBRARIES}"
    )
endif()

# ─── Main executable ──────────────────────────────────────────────────────────
add_executable(lua_ftxui
  src/main.cpp
  src/lua_ftxui.cpp
)

target_include_directories(lua_ftxui PRIVATE
  src
  "${lua_src_SOURCE_DIR}/src"
)

target_link_libraries(lua_ftxui PRIVATE
  Lua::Lua
  ftxui::screen
  ftxui::dom
  ftxui::component
)

# ─── Copy Lua scripts next to the binary ──────────────────────────────────────
file(COPY scripts/ DESTINATION "${CMAKE_BINARY_DIR}/scripts")

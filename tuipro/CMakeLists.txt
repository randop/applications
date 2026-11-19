cmake_minimum_required(VERSION 3.16)
project(tuipro VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# LuaJIT discovery
#
# tuipro embeds LuaJIT (5.1 API).  By default the system LuaJIT is used.  If
# pkg-config is unavailable, TUIPRO_LUAJIT_ROOT may point at a LuaJIT source
# checkout whose src/ contains lua.h and a built libluajit-5.1.  It can be set
# as a CMake cache variable (-DTUIPRO_LUAJIT_ROOT=/path/to/LuaJIT) or via the
# environment variable of the same name.  As a convenience, a checkout in a
# sibling directory of this project is also searched.
# ---------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LUAJIT QUIET luajit)
endif()

if(NOT LUAJIT_FOUND)
  set(TUIPRO_LUAJIT_ROOT "$ENV{TUIPRO_LUAJIT_ROOT}" CACHE PATH
      "Path to a LuaJIT source checkout (containing src/)")

  set(LUAJIT_INCLUDE_HINTS
      "${CMAKE_CURRENT_SOURCE_DIR}/../LuaJIT/src"
      "/usr/local/include/luajit-2.1"
      "/usr/include/luajit-2.1")
  set(LUAJIT_LIBRARY_HINTS
      "${CMAKE_CURRENT_SOURCE_DIR}/../LuaJIT/src")
  if(TUIPRO_LUAJIT_ROOT)
    list(PREPEND LUAJIT_INCLUDE_HINTS "${TUIPRO_LUAJIT_ROOT}/src")
    list(PREPEND LUAJIT_LIBRARY_HINTS "${TUIPRO_LUAJIT_ROOT}/src")
  endif()
  find_path(LUAJIT_INCLUDE_DIR lua.h HINTS ${LUAJIT_INCLUDE_HINTS})
  find_library(LUAJIT_LIBRARY NAMES luajit-5.1 libluajit-5.1
               HINTS ${LUAJIT_LIBRARY_HINTS})
  if(NOT LUAJIT_INCLUDE_DIR OR NOT LUAJIT_LIBRARY)
    message(FATAL_ERROR
      "LuaJIT not found. Install LuaJIT 2.1 development files, or set\n"
      "  -DTUIPRO_LUAJIT_ROOT=/path/to/LuaJIT\n"
      "after building LuaJIT (make -C /path/to/LuaJIT).")
  endif()
  set(LUAJIT_INCLUDE_DIRS ${LUAJIT_INCLUDE_DIR})
  set(LUAJIT_LIBRARIES ${LUAJIT_LIBRARY})
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(tuipro
  src/main.c
  src/term.c
  src/grid.c
  src/input.c
  src/lua_core.c
)

target_include_directories(tuipro PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${LUAJIT_INCLUDE_DIRS}
)

target_compile_definitions(tuipro PRIVATE
  _GNU_SOURCE
  _XOPEN_SOURCE=700
  TUIPRO_VERSION="${PROJECT_VERSION}"
  TUIPRO_LUA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/tuipro/lua"
)

target_compile_options(tuipro PRIVATE
  -Wall -Wextra -Wshadow -Wno-unused-parameter
  $<$<CONFIG:Debug>:-g3>
)

target_link_libraries(tuipro PRIVATE ${LUAJIT_LIBRARIES} m)

set_target_properties(tuipro PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS tuipro RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY lua/ DESTINATION ${CMAKE_INSTALL_DATADIR}/tuipro/lua
        FILES_MATCHING PATTERN "*.lua")
install(DIRECTORY examples/ DESTINATION ${CMAKE_INSTALL_DATADIR}/tuipro/examples
        FILES_MATCHING PATTERN "*.lua")
install(DIRECTORY plugins/ DESTINATION ${CMAKE_INSTALL_DATADIR}/tuipro/plugins
        FILES_MATCHING PATTERN "*.lua")
install(FILES include/tuipro.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/tuipro)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/tuipro/docs
        FILES_MATCHING PATTERN "*.md")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME lua_api
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(lua_api PROPERTIES
  ENVIRONMENT "TUIPRO_BIN=$<TARGET_FILE:tuipro>"
)

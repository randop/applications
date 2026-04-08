project(
    'neotui',
    'cpp',
    version: '0.1.17',
    meson_version: '>=0.60.0',
    default_options: ['cpp_std=c++20', 'default_library=static', 'b_staticpic=true'],
)

cmake = import('cmake')

ftxui_opts = cmake.subproject_options()
ftxui_opts.add_cmake_defines({'CMAKE_BUILD_TYPE': 'Release'})
ftxui_subproj = cmake.subproject('ftxui', options: ftxui_opts)
ftxui_component_dep = ftxui_subproj.dependency('component')
ftxui_screen_dep = ftxui_subproj.dependency('screen')
ftxui_dom_dep = ftxui_subproj.dependency('dom')

lua_dep = dependency(
    'lua',
    version: '>=5.5',
    fallback: 'lua',
    default_options: ['default_library=static'],
    required: true,
)

inc = include_directories('include')

sources = ['src/main.cpp', 'src/tui.cpp', 'src/config.cpp', 'src/plugins.cpp', 'src/scroller.cpp', 'src/bridge.cpp']

executable(
    'neotui',
    sources,
    dependencies: [
        lua_dep,
        ftxui_component_dep,
        ftxui_screen_dep,
        ftxui_dom_dep,
    ],
    include_directories: inc,
    link_args : ['-static-libgcc', '-static-libstdc++'],
    install: false,
)

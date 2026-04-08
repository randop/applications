project('ftxui_text_editor', 'cpp',
  version: '1.0.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
    'werror=false'
  ]
)

# Add ftxui as cmake subproject
cmake = import('cmake')
ftxui_opts = cmake.subproject_options()
ftxui_opts.add_cmake_defines({'FTXUI_BUILD_EXAMPLES': 'OFF', 'FTXUI_BUILD_TESTS': 'OFF', 'FTXUI_ENABLE_INSTALL': 'OFF'})
ftxui_proj = cmake.subproject('ftxui', options: ftxui_opts)
ftxui_dep = [
  ftxui_proj.dependency('screen'),
  ftxui_proj.dependency('dom'),
  ftxui_proj.dependency('component')
]

# Executable
executable('text_editor',
  ['src/main.cpp', 'src/text_editor.cpp', 'src/app.cpp'],
  dependencies: [ftxui_dep],
  install: true
)
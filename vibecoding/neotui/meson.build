project('neotui', 'cpp',
  version: '0.1.7',
  meson_version: '>=0.60.0',
  default_options: ['cpp_std=c++20']
)

cmake = import('cmake')

ftxui_opts = cmake.subproject_options()
ftxui_opts.add_cmake_defines({'CMAKE_BUILD_TYPE': 'Release'})
ftxui_subproj = cmake.subproject('ftxui', options: ftxui_opts)
ftxui_dep = ftxui_subproj.dependency('component')

lua_dep = dependency('lua', version: '>=5.5')

inc = include_directories('include')

sources = [
  'src/main.cpp',
  'src/tui.cpp',
  'src/config.cpp',
  'src/plugins.cpp'
]

executable('neotui',
  sources,
  dependencies : [lua_dep, ftxui_dep],
  include_directories : inc,
  install: false
)

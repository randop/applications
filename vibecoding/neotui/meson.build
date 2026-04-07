project('neotui', 'cpp',
  version: '0.1.4',
  default_options: ['cpp_std=c++20']
)

cmake = import('cmake')

ftxui_opts = cmake.subproject_options()
ftxui_opts.add_cmake_defines({'CMAKE_BUILD_TYPE': 'Release'})
ftxui_subproj = cmake.subproject('ftxui', options: ftxui_opts)
ftxui_dep = ftxui_subproj.dependency('component')

lua_dep = dependency('lua')

executable('neotui',
  'src/main.cpp',
  dependencies : [lua_dep, ftxui_dep],
  install: false
)

project('tui', ['c', 'cpp'],
  version: '0.1.0',
  default_options: [
    'buildtype=release',
    'optimization=3',
    'strip=true',
    'b_lto=true',
    'b_ndebug=true',
    'cpp_std=c++20',
    'c_std=c11'
  ],
  meson_version: '>= 1.0.1')

###############################################################################
# Compiler Options
###############################################################################
cpp = meson.get_compiler('cpp')
if cpp.get_id() == 'gcc' or cpp.get_id() == 'clang'
  add_project_arguments(
    [
      '-march=native', # Optimize for the host CPU
      '-mtune=native', # Tune for the host CPU
      '-ffast-math', # Enable fast math optimizations
      '-flto', # Ensure LTO is enabled
    ],
    language: 'cpp',
  )
  add_project_link_arguments(
    [
      '-flto', # LTO for linker
      '-Wl,--gc-sections', # Remove unused sections
    ],
    language: 'cpp',
  )
endif

if cpp.get_id() == 'clang'
  add_project_link_arguments('-static-libstdc++', language: 'cpp')
endif

###############################################################################
# Includes
###############################################################################
inc_dirs = []

###############################################################################
# Dependencies
###############################################################################
deps = []

cmake = import('cmake')

ftxui_opts = cmake.subproject_options()
ftxui_opts.add_cmake_defines({
  'CMAKE_BUILD_TYPE': 'Release',
  'CMAKE_POLICY_DEFAULT_CMP0156': 'NEW',
  'CMAKE_POLICY_DEFAULT_CMP0179': 'NEW',
  'FTXUI_BUILD_DOCS': 'OFF',
  'FTXUI_BUILD_EXAMPLES': 'OFF',
  'FTXUI_BUILD_MODULES': 'OFF',
  'FTXUI_BUILD_TESTS': 'OFF',
  'FTXUI_BUILD_TESTS_FUZZER': 'OFF',
  'FTXUI_CLANG_TIDY': 'OFF',
  'FTXUI_DEV_WARNINGS': 'OFF',
  'FTXUI_ENABLE_COVERAGE': 'OFF',
  'FTXUI_ENABLE_INSTALL': 'OFF',
  'FTXUI_QUIET': 'OFF'
})
ftxui = cmake.subproject('ftxui', options: ftxui_opts)
ftxui_screen_dep = ftxui.dependency('screen')
ftxui_dom_dep = ftxui.dependency('dom')
ftxui_component_dep = ftxui.dependency('component')

executable('tui',
  'main.cpp',
  include_directories: inc_dirs,
  dependencies: [ftxui_screen_dep, ftxui_dom_dep, ftxui_component_dep],
  link_args: ['-Wl,--gc-sections', '-Wl,-O2', '-static'])

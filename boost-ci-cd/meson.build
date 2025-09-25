project(
  'boost-app',
  'cpp',
  version: run_command('cat', 'VERSION', check: true).stdout().strip(),
  default_options: [
    'buildtype=release',
    'optimization=3',
    'strip=true',
    'b_lto=true',
    'b_ndebug=true',
    'cpp_std=c++20'
  ],
  meson_version: '>= 1.0.1',
)

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

###############################################################################
# Dependencies
###############################################################################

thread_dep = dependency('threads')

boost_dep = dependency(
  'boost',
  version: '>=1.89.0',
  modules: ['filesystem', 'thread', 'url', 'program_options'],
  required: true,
  static: true
)

###############################################################################
# Sources
###############################################################################

sources = ['src/main.cpp']

executable(
  'app',
  sources,
  dependencies: [
    thread_dep,
    boost_dep,
  ],
  cpp_args: ['-DBOOST_ALL_NO_LIB']
)


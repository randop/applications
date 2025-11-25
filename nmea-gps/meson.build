project('nmeagps', ['c', 'cpp'],
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

nema_sub = subproject(
  'NemaTode',
  default_options: [
    'cpp_std=c++17',
    'warning_level=0',
    'werror=false'
  ]
)

nematode_dep = nema_sub.get_variable('nematode_dep')

deps = [nematode_dep]

executable('nmeagps',
  'main.cpp',
  include_directories: inc_dirs,
  dependencies: deps,
  link_args: ['-Wl,--gc-sections', '-Wl,-O2', '-static'])

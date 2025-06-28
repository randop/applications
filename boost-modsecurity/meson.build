project(
  'boost-modsec',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'optimization=3',
    'debug=false',
    'b_lto=true', # Enable link-time optimization
    'b_ndebug=true', # Disable debug assertions
    'warning_level=3',
  ],
  meson_version: '>= 1.0.1',
)

# Compiler setup for performance
cpp = meson.get_compiler('cpp')
add_project_arguments(
  '-march=native', # Optimize for host CPU
  '-ffast-math', # Fast floating-point optimizations
  '-fno-exceptions', # Disable exceptions for performance
  '-fexceptions',
  language: 'cpp',
)

# Dependencies
libmodsecurity_dep = dependency('modsecurity', version: '>=3.0.12', required: true)
boost_dep = dependency(
  'boost',
  version: '>=1.83.0',
  modules: ['system', 'coroutine', 'thread', 'context', 'json'],
  required: true,
)

# Source files
sources = ['src/httpServer.cpp', 'src/main.cpp', 'src/modSecurityFilter.cpp']

# Executable
executable(
  'boost-modsec',
  sources: sources,
  include_directories: include_directories('src'),
  dependencies: [libmodsecurity_dep, boost_dep],
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic'],
  link_args: ['-flto', '-fno-rtti'],
)
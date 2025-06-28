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
  '-fno-rtti', # Disable RTTI for lower memory usage
  language: 'cpp',
)

# Dependencies
libmodsecurity_dep = dependency('libmodsecurity', required: true)
boost_dep = dependency(
  'boost',
  version: '>=1.88.0',
  modules: ['system', 'coroutine', 'thread', 'context', 'json'],
  required: true,
)

# Source files
sources = ['src/http_server.cpp', 'src/main.cpp', 'src/modsecurity_filter.cpp']

# Executable
executable(
  'boost-modsec',
  sources: sources,
  include_directories: include_directories('include'),
  dependencies: [libmodsecurity_dep, boost_dep],
  cpp_args: ['-Wall', '-Wextra', '-Wpedantic'],
  link_args: ['-flto', '-fno-rtti'],
)
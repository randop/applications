project('cpplua', 'cpp',
  version: '0.1.0',
  default_options: ['warning_level=3', 'cpp_std=c++20'])

lua_dep = dependency('lua', version: '>=5.3')

executable('cpplua',
  'main.cpp',
  dependencies: lua_dep,
  install: true)

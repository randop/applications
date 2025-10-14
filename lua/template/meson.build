project('cpplua-template', ['c', 'cpp'],
  version: '0.1.0',
  default_options: ['warning_level=3', 'cpp_std=c++20', 'c_std=c11'],
  meson_version: '>= 1.0.1')

deps = []

lua_proj = subproject('lua')
lua_dep = lua_proj.get_variable('lua_dep')
deps+=lua_dep

sources = [
  'src/main.cpp'
]

executable('app',
  sources,
  dependencies: deps)

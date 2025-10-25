project('vrp', ['c', 'cpp'],
  version: '0.1.0',
  default_options: ['warning_level=3', 'cpp_std=c++20', 'c_std=c11'],
  meson_version: '>= 1.0.1')

deps = []

lua_proj = subproject('lua')
lua_dep = lua_proj.get_variable('lua_dep')
deps+=lua_dep

openssl_dep = dependency('openssl')
deps+=openssl_dep

luasec_proj = subproject('luasec')
luasec_dep = luasec_proj.get_variable('luasec_dep')
deps+=luasec_dep

sources = [
  'src/main.cpp'
]

executable('vrp',
  sources,
  dependencies: deps)

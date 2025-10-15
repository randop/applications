project('cppsodium', 'cpp',
  version: '0.1.0',
  default_options: ['warning_level=3', 'cpp_std=c++20'])

sodium_dep = dependency('libsodium', version: '>=1.0.20', required: true)

executable('cppsodium',
  'main.cpp',
  dependencies: sodium_dep)

project('stackless-https-server', 'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=3',
  ]
)

boost_dep = dependency('boost', modules: ['system'], required: true)
openssl_dep = dependency('openssl', required: true)

executable('https_server',
  'main.cpp',
  dependencies: [boost_dep, openssl_dep],
  install: false
)

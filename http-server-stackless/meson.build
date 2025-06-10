project(
    'http-server-stackless',
    'cpp',
    version : '1.0.0',
    default_options : [
        'buildtype=release',
        'cpp_std=c++17'
    ],
    meson_version: '>= 1.0.1',
)

boost_mods = [
    'system',
    'filesystem',
    'thread',
    'json',
    'program_options'
]
boost_dep = dependency('boost', version: '>=1.88.0', modules: boost_mods, required: true)

openssl_dep = dependency('openssl', required: true)

executable('http-server-stackless',
           'src/main.cpp',
           dependencies: [boost_dep, openssl_dep],
)


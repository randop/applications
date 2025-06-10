project(
    'boost-json',
    'cpp',
    version : '1.0.0',
    default_options : [
        'buildtype=debug',
        'optimization=0',
        'cpp_std=c++17'
    ],
    meson_version: '>= 0.60.0',
)

boost_dep = dependency('boost', version: '>=1.88.0', modules: ['system', 'filesystem', 'thread', 'json'], required: true)

executable('boost-json',
           'main.cpp',
           dependencies: [boost_dep],
)


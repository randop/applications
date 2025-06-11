project(
    'blog',
    'cpp',
    version : '1.0.0',
    default_options : [
        'buildtype=debugoptimized',
        'optimization=0',
        'cpp_std=c++17'
    ],
    meson_version: '>= 1.0.1',
)

###############################################################################
# Dependencies
###############################################################################
cmake = import('cmake')

libpqxx = cmake.subproject('libpqxx')
libpqxx_dep = libpqxx.get_variable('pqxx_dep')

thread_dep = dependency('threads')

###############################################################################
# Sources
###############################################################################

sources = [
    'src/main.cpp'
]

executable(
    'blog',
    sources,
    dependencies: [
        libpqxx_dep,
        thread_dep
    ]
)

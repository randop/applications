project(
  'blog',
  'cpp',
version : '1.0.0',
default_options : ['buildtype=debugoptimized', 'optimization=0', 'cpp_std=c++20'],
meson_version : '>= 1.0.1'
)

###############################################################################
# Dependencies
###############################################################################
cmake = import('cmake')

libpqxx = cmake.subproject('libpqxx')
libpqxx_dep = libpqxx.get_variable('pqxx_dep')

thread_dep = dependency('threads')

###############################################################################
# Includes
###############################################################################
inc_src = include_directories('src')
inc_inc = include_directories('src/include')
inc_dirs = [
    inc_src,
    inc_inc
]

###############################################################################
# Sources
###############################################################################

sources = [
    'src/main.cpp',
    'src/dbConnection.cpp',
    'src/connectionPool.cpp'
]

executable(
    'blog',
    sources,
    dependencies: [
        libpqxx_dep,
        thread_dep
    ],
    include_directories: inc_dirs,
)

project(
  'blog',
  'cpp',
  version : '1.0.0',
  default_options : [
        'buildtype=release',
        'optimization=3',
        'strip=true',
        'b_lto=true',
        'b_ndebug=true',
        'cpp_std=c++20'
  ],
  meson_version : '>= 1.0.1'
)

###############################################################################
# Dependencies
###############################################################################
cmake = import('cmake')

libpqxx = cmake.subproject('libpqxx')
libpqxx_dep = libpqxx.get_variable('pqxx_dep')

cmark = cmake.subproject('cmark')
cmark_dep = cmark.get_variable('cmark_dep')

thread_dep = dependency('threads')

boost_dep = dependency('boost', version: '>=1.88.0', modules: ['system', 'filesystem', 'thread', 'url'], required: true)

fmt = subproject('fmt')
fmt_dep = fmt.get_variable('fmt_dep')

spdlog = subproject('spdlog')
spdlog_dep = spdlog.get_variable('spdlog_dep')

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
# Compiler Options
###############################################################################
cpp = meson.get_compiler('cpp')
if cpp.get_id() == 'gcc' or cpp.get_id() == 'clang'
  add_project_arguments([
    '-march=native',          # Optimize for the host CPU
    '-mtune=native',          # Tune for the host CPU
    '-ffast-math',            # Enable fast math optimizations
    '-flto',                  # Ensure LTO is enabled
  ], language: 'cpp')
  add_project_link_arguments([
    '-flto',                  # LTO for linker
    '-Wl,--gc-sections',      # Remove unused sections
  ], language: 'cpp')
endif

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
        cmark_dep,
        thread_dep,
        boost_dep,
        fmt_dep,
        spdlog_dep
    ],
    include_directories: inc_dirs,
    link_args: ['-Wl,--gc-sections', '-Wl,-O2']
)

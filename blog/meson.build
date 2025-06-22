project(
  'blog',
  'cpp',
  version: run_command('cat', 'VERSION', check: true).stdout().strip(),
  default_options: [
    'buildtype=release',
    'optimization=3',
    'strip=true',
    'b_lto=true',
    'b_ndebug=true',
    'cpp_std=c++20',
  ],
  meson_version: '>= 1.0.1',
)

###############################################################################
# Versioning
###############################################################################
version = meson.project_version()
increment_script = run_command('sh', 'increment_build.sh', check: true)
build_number = run_command('cat', 'build.number', check: true).stdout().strip()
configure_file(
  input: 'src/project.hpp.in',
  output: 'project.hpp',
  configuration: {'VERSION': version, 'BUILD': build_number},
)

###############################################################################
# Dependencies
###############################################################################
deps = []

cmake = import('cmake')

cmark = cmake.subproject('cmark')
cmark_dep = cmark.get_variable('cmark_dep')

thread_dep = dependency('threads')

boost_dep = dependency(
  'boost',
  version: '>=1.88.0',
  modules: ['system', 'filesystem', 'thread', 'url', 'program_options'],
  required: true,
  static: true,
)

fmt = subproject('fmt', default_options: ['default_library=static', 'header-only=true'])
fmt_dep = fmt.get_variable('fmt_dep')

spdlog = subproject(
  'spdlog',
  default_options: ['default_library=static', 'compile_library=false'],
)
spdlog_dep = spdlog.get_variable('spdlog_dep')

openssl_dep = dependency('openssl', required: true)

mongocxx_dep = dependency('mongocxx', version: '>=4.1.0', required: false)
bsoncxx_dep = dependency('bsoncxx', version: '>=4.1.0', required: false)

if not mongocxx_dep.found() or not bsoncxx_dep.found()
  mongocxx = cmake.subproject(
    'mongo-cxx-driver',
    cmake_options: [
      '-DCMAKE_PREFIX_PATH=/opt/mongo-c-driver/current',
    ],
  )
  mongocxx_dep = mongocxx.dependency('mongocxx_static')
  bsoncxx_dep = mongocxx.dependency('bsoncxx_static')
endif

###############################################################################
# Includes
###############################################################################
inc_src = include_directories('src')
inc_inc = include_directories('src/include')
inc_dirs = [inc_src, inc_inc]

###############################################################################
# Compiler Options
###############################################################################
cpp = meson.get_compiler('cpp')
if cpp.get_id() == 'gcc' or cpp.get_id() == 'clang'
  add_project_arguments(
    [
      '-march=native', # Optimize for the host CPU
      '-mtune=native', # Tune for the host CPU
      '-ffast-math', # Enable fast math optimizations
      '-flto', # Ensure LTO is enabled
    ],
    language: 'cpp',
  )
  add_project_link_arguments(
    [
      '-flto', # LTO for linker
      '-Wl,--gc-sections', # Remove unused sections
    ],
    language: 'cpp',
  )
endif

###############################################################################
# Sources
###############################################################################

sources = ['src/main.cpp']

executable(
  'blog',
  sources,
  dependencies: [
    cmark_dep,
    thread_dep,
    boost_dep,
    fmt_dep,
    spdlog_dep,
    openssl_dep,
    mongocxx_dep,
    bsoncxx_dep,
  ],
  include_directories: inc_dirs,
  cpp_args: ['-DPQXX_HEADER_ONLY=1', '-DBOOST_ALL_NO_LIB', '-DFMT_HEADER_ONLY'],
  link_args: ['-Wl,--gc-sections', '-Wl,-O2'],
)
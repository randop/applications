project('methuselah',
  'cpp',
  version: run_command('cat', 'VERSION', check : true).stdout().strip(),
  default_options: [
    'cpp_std=c++20',
    'buildtype=release',
    'optimization=3',
    'strip=true',
    'b_lto=true',
    'b_ndebug=true'
  ],
  meson_version : '>= 1.0.1'
)

# Dependencies
cmake = import('cmake')

raylib_opts = cmake.subproject_options()
raylib_opts.add_cmake_defines({
    'BUILD_EXAMPLES': false,
    'BUILD_GAMES': false,
    'PLATFORM': 'Desktop',
    'GRAPHICS': 'GRAPHICS_API_OPENGL_33',
})
raylib_proj = cmake.subproject('raylib', options: raylib_opts)
raylib_dep = raylib_proj.dependency('raylib')

raygui_proj = subproject('raygui',
  default_options: [
    'warning_level=0',
    'werror=false',
  ]
)
raygui_inc = raygui_proj.get_variable('raygui_inc')

x11_dep  = dependency('x11')
gl_dep   = dependency('gl')
xi_dep   = dependency('xi')
xrandr_dep = dependency('xrandr')
xcursor_dep = dependency('xcursor')

gpgme_dep = dependency('gpgme', required: true)
gpgerror_dep = dependency('gpg-error', required: true)

boost_dep = dependency('boost',
  required: true,
  version: '>=1.90.0',
  modules: ['filesystem'])

spdlog_dep = dependency(
  'spdlog',
  version: '>=1.17',
  default_options: [
    'compile_library=false',
    'std_format=enabled',
  ],
)

assuan_dep = dependency('libassuan', required: true)

# Configuration
conf = configuration_data()
conf.set('PROJECT_NAME', meson.project_name())
conf.set('PROJECT_VERSION', meson.project_version())
conf.set('PROJECT_DESCRIPTION', 'pinentry helper')
if get_option('buildtype').startswith('release')
  conf.set('SPDLOG_DEFAULT_LEVEL', 'spdlog::level::info')
else
  conf.set('SPDLOG_DEFAULT_LEVEL', 'spdlog::level::trace')
endif

config_h = configure_file(
    input: 'config.h.in',
    output: 'config.h',
    configuration: conf
)

# Source files
sources = files(
    'src/application.cpp',
    'src/config.cpp',
    'src/git_credentials.cpp',
    'src/gpg_decrypt.cpp',
    'src/instance_lock.cpp',
    'src/logging.cpp',
    'src/main.cpp',
    'src/path.cpp',
    'src/pinentry_server.cpp',
    'src/signal_guard.cpp',
    'src/ui_session.cpp',
)

# Executable
executable('methuselah',
    sources,
    dependencies: [
      raylib_dep,
      x11_dep,
      gl_dep,
      xi_dep,
      xrandr_dep,
      xcursor_dep,
      gpgme_dep,
      gpgerror_dep,
      boost_dep,
      spdlog_dep,
      assuan_dep
    ],
    install: true,
    include_directories: [
      include_directories('include'),
      raygui_inc,
    ],
    cpp_args: [
      '-Wall',
      '-Wextra',
    ]
)

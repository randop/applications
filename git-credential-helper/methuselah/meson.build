project('methuselah',
        'cpp',
        version: '0.1.3',
        default_options: [
            'warning_level=0',
            'cpp_std=c++20',
            'buildtype=release'
        ])

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

raygui_proj = subproject('raygui')
raygui_inc = raygui_proj.get_variable('raygui_inc')

x11_dep  = dependency('x11')
gl_dep   = dependency('gl')
xi_dep   = dependency('xi')
xrandr_dep = dependency('xrandr')
xcursor_dep = dependency('xcursor')

gpgme_dep = dependency('gpgme', required: true)

boost_dep = dependency('boost',
  required: true,
  version: '>=1.90.0',
  modules: ['system', 'filesystem'])

# Configuration
conf = configuration_data()
conf.set('PROJECT_NAME', meson.project_name())
conf.set('PROJECT_VERSION', meson.project_version())
conf.set('PROJECT_DESCRIPTION', 'Git Credential Helper')

config_h = configure_file(
    input: 'config.h.in',
    output: 'config.h',
    configuration: conf
)

# Source files
sources = files(
    'src/main.cpp'
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
      boost_dep,
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

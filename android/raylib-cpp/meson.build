project('raylib-android-cpp', ['c', 'cpp'],
  version: run_command('cat', 'VERSION', check : true).stdout().strip(),
  default_options: [
    'cpp_std=c++23',
    'warning_level=2',
    'buildtype=release',
    'b_lto=true',
  ])

# --- Android configuration ---
ndk = '/home/johnpaul/opt/android/sdk/ndk/29.0.14206865'
api = '34'

# --- raylib as CMake subproject (static) ---
cmake = import('cmake')
raylib_opts = cmake.subproject_options()

raylib_opts.add_cmake_defines({
  'CMAKE_SYSTEM_NAME': 'Android',
  'CMAKE_SYSTEM_VERSION': api,
  'CMAKE_ANDROID_NDK': ndk,
  'CMAKE_ANDROID_ARCH_ABI': 'arm64-v8a',

  'ANDROID_NDK': ndk,

  'PLATFORM': 'Android',
  'BUILD_EXAMPLES': 'OFF',
  'BUILD_SHARED_LIBS': 'OFF',
  'CMAKE_BUILD_TYPE': 'MinSizeRel',
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

# native_app_glue is required
native_app_glue = files('/home/johnpaul/opt/android/sdk/ndk/29.0.14206865/sources/android/native_app_glue/android_native_app_glue.c')

inc = include_directories('include')

# native android application as shared library
lib = shared_library('main',
  sources: ['src/main.cpp', native_app_glue],
  dependencies: [raylib_dep],
  include_directories: [
    inc,
    include_directories('/home/johnpaul/opt/android/sdk/ndk/29.0.14206865/sources/android/native_app_glue'),
    raygui_inc,
  ],
  cpp_args: [
    '-DANDROID',
    '-DPLATFORM_ANDROID',
  ],
  link_args: [
    '-u', 'ANativeActivity_onCreate',
    '-Wl,--build-id',
    '-Wl,-z,noexecstack',
    '-Wl,-z,relro',
    '-Wl,-z,now',
    '-landroid',
    '-llog',
    '-lEGL',
    '-lGLESv2',
    '-lOpenSLES',
    '-latomic',
    '-lm',
    '-ldl',
  ],
  name_prefix: 'lib',
  name_suffix: 'so',
  install: false,
)


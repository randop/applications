project(
    'currency-converter',
    'cpp',
    version : '1.3.0',
    default_options : [
        'buildtype=release',
        'optimization=3',
        'strip=true',
        'b_lto=true',
        'b_ndebug=true',
        'cpp_std=c++17'
    ],
    meson_version: '>= 0.60.0',
)

boost_dep = dependency('boost', version: '>=1.88.0', modules: ['system', 'filesystem', 'thread', 'json', 'program_options'], required: true, static: true)

openssl_dep = dependency('openssl', required: true)

fmt = subproject('fmt', default_options: ['default_library=static'])
fmt_dep = fmt.get_variable('fmt_dep')

spdlog = subproject('spdlog', default_options: ['default_library=static', 'compile_library=false'])
spdlog_dep = spdlog.get_variable('spdlog_dep')

# Compiler-specific options for performance and size
cpp = meson.get_compiler('cpp')
if cpp.get_id() == 'gcc' or cpp.get_id() == 'clang'
  add_project_arguments([
    '-march=native',          # Optimize for the host CPU
    '-mtune=native',          # Tune for the host CPU
    '-ffast-math',            # Enable fast math optimizations
    '-fno-rtti',              # Disable RTTI to reduce binary size
    '-flto',                  # Ensure LTO is enabled
  ], language: 'cpp')
  add_project_link_arguments([
    '-flto',                  # LTO for linker
    '-Wl,--gc-sections',      # Remove unused sections
  ], language: 'cpp')
endif

inc_dirs = include_directories('src/include')

sources = [
    'src/main.cpp'
]

executable('currency-converter',
           sources,
           include_directories : inc_dirs,
           dependencies: [boost_dep, openssl_dep, fmt_dep, spdlog_dep],
           link_args: ['-Wl,--gc-sections', '-Wl,-O2'] # Optimize linker output
)

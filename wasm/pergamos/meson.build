project(
    'pergamos',
    'cpp',
    version: '1.0.0',
    license: 'Apache-2.0',
    meson_version: '>=1.0.0',
    default_options: [
        'cpp_std=c++20',
        'b_lto=true',
        'b_staticpic=true',
        'default_library=static',
        'warning_level=3',
    ],
)

sources = ['src/main.cpp']
inc_src = include_directories('src')
inc_dirs = [inc_src]

WASM_DIRECTIVES = host_machine.system() == 'wasi' or host_machine.cpu_family() == 'wasm32'

if WASM_DIRECTIVES
    # WASM target
    executable(
        'pergamos.wasm',
        sources,
        include_directories: inc_dirs,
        install: true,
    )
else
    # native target
    executable('pergamos', sources, include_directories: inc_dirs, install: true)
endif

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

# ================================================================
# wolfSSL static library
# ================================================================
wolfssl_root = '/opt/wolfssl/current'

wolfssl_inc = include_directories(wolfssl_root / 'include')

inc_src = include_directories('src')
inc_dirs = [inc_src, wolfssl_inc]

deps = []

WASM_DIRECTIVES = host_machine.system() == 'wasi' or host_machine.cpu_family() == 'wasm32'

if WASM_DIRECTIVES
    # WASM target
    executable(
        'pergamos.wasm',
        sources,
        include_directories: inc_dirs,
        dependencies: deps,
        install: true,
    )
else
    # native target
    executable(
        'pergamos',
        sources,
        include_directories: inc_dirs,
        dependencies: deps,
        link_args: [wolfssl_root / 'lib' / 'libwolfssl.a'],
        install: true,
    )
endif

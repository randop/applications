project(
    'sardis',
    'cpp',
    version: '0.1.0',
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


if host_machine.system() == 'wasi' or host_machine.cpu_family() == 'wasm32'
    # WASM target
    # ```sh
    # meson setup .build-wasm --cross-file cross/wasm-wasi.txt --buildtype=release --default-library=static
    # ```
    wasm_exe = executable('sardis.wasm', 'src/entrypoint.cpp', install: true)
else
    # native target
    executable('sardis', 'src/main.cpp', install: true)
endif

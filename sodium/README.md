# CppSodium

C++ project demo that secures a string using libsodium.

## Project Development
1. Install toolchain packages: `pacman -S meson libsodium`
2. Verify libsodium headers: `pacman -Ql libsodium | grep include`
3. Setup build: `meson setup build`
4. Compile: `meson compile -C build`
5. Execute: `./build/cppsodium`

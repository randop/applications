#!/bin/sh
# Compiles currency.cpp to a freestanding wasm32 module.
# Requires clang/clang++ with the wasm32 target and lld (LLVM's linker).
# On Debian/Ubuntu:  apt-get install clang lld
set -eu

cd "$(dirname "$0")"

clang++ \
  --target=wasm32 \
  -nostdlib \
  -fno-exceptions \
  -fno-rtti \
  -fvisibility=hidden \
  -O2 \
  -Wl,--no-entry \
  -Wl,--export=alloc \
  -Wl,--export=reset_arena \
  -Wl,--export=arena_capacity \
  -Wl,--export=convert \
  -Wl,--export=batch_convert \
  -Wl,--export=memory \
  -Wl,--allow-undefined \
  -Wl,-z,stack-size=1048576 \
  -o currency.wasm \
  currency.cpp

echo "built $(pwd)/currency.wasm ($(wc -c < currency.wasm) bytes)"

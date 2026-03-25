#!/bin/sh

rm -fv ip_strings_wasm.o ip_ranges_wasm.o

echo "Compiling ip_strings..." && /opt/wasi-sdk-32/bin/clang --target=wasm32-wasip1 -c ip_strings_wasm.s -o ip_strings_wasm.o
echo "Compiling ip_ranges..." && /opt/wasi-sdk-32/bin/clang --target=wasm32-wasip1 -c ip_ranges_wasm.s -o ip_ranges_wasm.o

echo "Checking ip_strings..." && /opt/wasi-sdk-32/bin/llvm-nm ip_strings_wasm.o
echo "Checking ip_ranges..." && /opt/wasi-sdk-32/bin/llvm-nm ip_strings_wasm.o

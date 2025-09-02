#!/usr/bin/env bash
clang -target x86_64-pc-none-elf \
  -S -emit-llvm \
  -fno-stack-protect \
  -nostdinc \
  -I/path/to/picolibc/include \
  -I/path/to/uefi-headers \
  -o main.ll main.c

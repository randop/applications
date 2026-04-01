#!/bin/sh

set -e

clang-format -i module.c

rm -fv server &&
  gcc -O2 -Wall -Wno-unused-variable -o module module.c \
    $(pkg-config --cflags --libs libuv) &&
  chmod +x module &&
  ./module

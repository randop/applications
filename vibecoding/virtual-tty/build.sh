#!/bin/sh

set -e

clang-format -i main.c

rm -fv virtual-tty &&
  gcc -O2 -Wall -Wno-unused-variable -o virtual-tty main.c \
    $(pkg-config --cflags --libs libuv) &&
  chmod +x virtual-tty &&
  ./virtual-tty

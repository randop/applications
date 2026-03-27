#!/bin/sh

set -e

clang-format -i server.c

rm -fv server &&
  gcc -O2 -Wall -o server server.c \
    $(pkg-config --cflags --libs libuv) &&
  chmod +x server &&
  ./server

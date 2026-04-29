#!/bin/sh

set -eu

export LD_LIBRARY_PATH="$HOME/opt/boringssl/current/lib:$HOME/opt/lsquic/current/lib"

g++ -O3 main.cpp -o server.bin \
  -I$HOME/opt/lsquic/current/include/lsquic \
  -L$HOME/opt/lsquic/current/lib \
  -L$HOME/opt/boringssl/current/lib \
  -Wl,-rpath,$HOME/opt/lsquic/current/lib \
  -Wl,-rpath,$HOME/opt/boringssl/current/lib \
  -llsquic -lssl -lcrypto -lz -luring -lpthread -ldl

#!/bin/sh

set -eu

export LD_LIBRARY_PATH="$HOME/opt/boringssl/current/lib:$HOME/opt/lsquic/current/lib"

rm -fv server.bin
g++ -O3 server.cpp -o server.bin \
  -I$HOME/opt/lsquic/current/include/lsquic \
  -L$HOME/opt/lsquic/current/lib \
  -L$HOME/opt/boringssl/current/lib \
  -Wl,-rpath,$HOME/opt/lsquic/current/lib \
  -Wl,-rpath,$HOME/opt/boringssl/current/lib \
  -llsquic -lssl -lcrypto -lz -luring -lpthread -ldl

rm -fv client.bin
g++ -O3 -std=c++20 client.cpp -o client.bin \
  -I$HOME/opt/lsquic/current/include/lsquic \
  -L$HOME/opt/lsquic/current/lib \
  -L$HOME/opt/boringssl/current/lib \
  -Wl,-rpath,$HOME/opt/lsquic/current/lib \
  -Wl,-rpath,$HOME/opt/boringssl/current/lib \
  -llsquic -lssl -lcrypto -lz -luring -lpthread

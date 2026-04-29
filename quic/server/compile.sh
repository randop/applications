#!/bin/sh

set -eu

export LD_LIBRARY_PATH="$HOME/opt/boringssl/current:$HOME/opt/lsquic/current"
gcc -o server.bin main.c -llsquic -lssl -lcrypto -lz -luring -lpthread -I$HOME/opt/lsquic/current/include/lsquic

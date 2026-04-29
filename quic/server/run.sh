#!/bin/sh

set -eu

export LD_LIBRARY_PATH="$HOME/opt/boringssl/current/lib:$HOME/opt/lsquic/current/lib"

./server.bin 0.0.0.0 4433

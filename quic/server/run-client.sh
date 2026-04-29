#!/bin/sh

set -eu

export LD_LIBRARY_PATH="$HOME/opt/boringssl/current/lib:$HOME/opt/lsquic/current/lib"

stdbuf -oL ./client.bin 127.0.0.1 4433

#!/bin/sh

set -eu

rm -f .build/service

g++ -std=c++23 -Wall -Wextra -o .build/service src/main.cpp src/client.cpp \
  -I include \
  -I $HOME/opt/tigerbeetle/0.17.4/src/clients/c \
  -L $HOME/opt/tigerbeetle/0.17.4/src/clients/c/lib/x86_64-linux-gnu.2.27 \
  -l:libtb_client.a \
  -lpthread -ldl -lm

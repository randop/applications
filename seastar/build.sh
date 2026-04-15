#!/bin/sh

set -eu

export OPT_PREFIX="$HOME/opt"
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig"
export BOOST_ROOT="$HOME/opt/boost/current"
export CMAKE_PREFIX_PATH="$HOME/opt/seastar/current"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/boost/current:$CMAKE_PREFIX_PATH"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/fmt/current:${CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/hwloc/current:${CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/protobuf/current:${CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/lksctp/current:${CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/colm-suite/current:${CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/valgrind/current:${CMAKE_PREFIX_PATH}"

echo "CMAKE_PREFIX_PATH: $CMAKE_PREFIX_PATH"

clang-format -i main.cc

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

make -j$(nproc)

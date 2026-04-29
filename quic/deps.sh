#!/bin/sh

# exit on error, error on unset variables
set -eu

OPT_PREFIX=$HOME/opt

if [ ! -d "${OPT_PREFIX}" ]; then
  mkdir -p ${OPT_PREFIX}
else
  echo "${OPT_PREFIX} directory: OK"
fi

LOCAL_PKGCONFIG=${HOME}/.local/lib/pkgconfig
if [ ! -d "${LOCAL_PKGCONFIG}" ]; then
  mkdir -p ${LOCAL_PKGCONFIG}
fi

LOCAL_BIN=${HOME}/.local/bin
if [ ! -d "${LOCAL_BIN}" ]; then
  mkdir -p ${LOCAL_BIN}
fi

if [ -n "${PKG_CONFIG_PATH+set}" ]; then
  export PKG_CONFIG_PATH="${LOCAL_PKGCONFIG}:${PKG_CONFIG_PATH}"
else
  export PKG_CONFIG_PATH=$LOCAL_PKGCONFIG
fi

BORINGSSL_VERSION=0.20260413.0
if [ ! -f "${OPT_PREFIX}/boringssl/current/lib/libssl.a" ]; then
  echo "Compiling boringssl..."
  mkdir -p ${OPT_PREFIX}/boringssl/current
  rm -rf ${OPT_PREFIX}/boringssl/${BORINGSSL_VERSION}
  git clone -b ${BORINGSSL_VERSION} https://boringssl.googlesource.com/boringssl ${OPT_PREFIX}/boringssl/${BORINGSSL_VERSION}
  rm -rf ${OPT_PREFIX}/boringssl/${BORINGSSL_VERSION}/.git
  rm -rf ${OPT_PREFIX}/boringssl/${BORINGSSL_VERSION}/.github
  cd ${OPT_PREFIX}/boringssl/${BORINGSSL_VERSION}
  mkdir -p ${OPT_PREFIX}/boringssl/${BORINGSSL_VERSION}/build
  cmake -GNinja -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${OPT_PREFIX}/boringssl/current
  ninja -C build -j$(nproc) install
  echo "Checking compiled boringssl library..." && file ${OPT_PREFIX}/boringssl/current/lib/libssl.a
else
  echo "boringssl: OK"
fi

if [ -n "${CMAKE_PREFIX_PATH+set}" ]; then
  CMAKE_PREFIX_PATH="${OPT_PREFIX}/boringssl/current:${CMAKE_PREFIX_PATH}"
else
  CMAKE_PREFIX_PATH="${OPT_PREFIX}/boringssl/current"
fi
export CMAKE_PREFIX_PATH

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

export BORINGSSL_INSTALL_DIR="${OPT_PREFIX}/boringssl/current"
export BORINGSSL_DIR="${OPT_PREFIX}/boringssl/${BORINGSSL_VERSION}"

if [ -n "${CMAKE_PREFIX_PATH+set}" ]; then
  CMAKE_PREFIX_PATH="${OPT_PREFIX}/boringssl/current:${CMAKE_PREFIX_PATH}"
else
  CMAKE_PREFIX_PATH="${OPT_PREFIX}/boringssl/current"
fi
export CMAKE_PREFIX_PATH
echo $CMAKE_PREFIX_PATH

LSQUIC_VERSION=v4.6.3
if [ ! -f "${OPT_PREFIX}/lsquic/current/lib/liblsquic.a" ]; then
  echo "Compiling lsquic..."
  mkdir -p ${OPT_PREFIX}/lsquic/current
  rm -rf ${OPT_PREFIX}/lsquic/${LSQUIC_VERSION}
  git clone -b ${LSQUIC_VERSION} https://github.com/litespeedtech/lsquic.git ${OPT_PREFIX}/lsquic/${LSQUIC_VERSION}
  cd ${OPT_PREFIX}/lsquic/${LSQUIC_VERSION}
  git submodule update --init
  rm -rf ${OPT_PREFIX}/lsquic/${LSQUIC_VERSION}/.git
  rm -rf ${OPT_PREFIX}/lsquic/${LSQUIC_VERSION}/.github
  mkdir -p ${OPT_PREFIX}/lsquic/${LSQUIC_VERSION}/build
  cd ${OPT_PREFIX}/lsquic/${LSQUIC_VERSION}/build
  cmake .. -DLIBSSL_DIR=$BORINGSSL -DCRYPTO_LIB=$BORINGSSL -DBUILD_SHARED_LIBS=1 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${OPT_PREFIX}/lsquic/current
  make -j$(nproc) install
  echo "Checking compiled lsquic library..." && file ${OPT_PREFIX}/lsquic/current/lib/liblsquic.a
else
  echo "lsquic: OK"
fi

export CMAKE_PREFIX_PATH="${OPT_PREFIX}/lsquic/current:${CMAKE_PREFIX_PATH}"

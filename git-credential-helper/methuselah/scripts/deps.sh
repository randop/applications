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

export BOOST_ROOT=${OPT_PREFIX}/boost/current

if [ -n "${CMAKE_PREFIX_PATH+set}" ]; then
  CMAKE_PREFIX_PATH="${OPT_PREFIX}/boost/current:${CMAKE_PREFIX_PATH}"
else
  CMAKE_PREFIX_PATH="${OPT_PREFIX}/boost/current"
fi
export CMAKE_PREFIX_PATH

GPGME_VERSION=1.23.2
GPGME_TAG=gpgme-1.23.2
if [ ! -f "${OPT_PREFIX}/gpgme/current/lib/gpgme.a" ]; then
  mkdir -p ${OPT_PREFIX}/gpgme/current
  rm -rf ${OPT_PREFIX}/gpgme/current/*
  rm -rf ${OPT_PREFIX}/gpgme/${GPGME_VERSION}
  git clone -b ${GPGME_TAG} https://github.com/gpg/gpgme.git ${OPT_PREFIX}/gpgme/${GPGME_VERSION}
  rm -rf ${OPT_PREFIX}/gpgme/${GPGME_VERSION}/.git
  cd ${OPT_PREFIX}/gpgme/${GPGME_VERSION}
  ./autogen.sh
  ./configure --prefix=${OPT_PREFIX}/gpgme/current MAKEINFO=true
  make -j$(nproc)
  make install
  rm -f $LOCAL_PKGCONFIG/gpgme.pc
else
  echo "gpgme: OK"
fi

if [ ! -f "${LOCAL_PKGCONFIG}/gpgme.pc" ]; then
  ln -sv ${OPT_PREFIX}/gpgme/current/lib/pkgconfig/gpgme.pc ${LOCAL_PKGCONFIG}/gpgme.pc
fi
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/gpgme/current:${CMAKE_PREFIX_PATH}"

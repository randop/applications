#!/bin/sh
set -eu

OPT_PREFIX=$HOME/opt

if [ ! -d "${OPT_PREFIX}" ]; then
  echo "ERROR: $HOME/opt directory is missing!"
  exit 1
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
if [ ! -f "${OPT_PREFIX}/boost/current/lib/libboost_atomic.so" ]; then
  echo "boost: missing!!!"
fi
BOOST_VERSION=v1.91.0
BOOST_STRING=boost-1.91.0-1
export BOOST_ROOT=${OPT_PREFIX}/boost/current

if [ -n "${CMAKE_PREFIX_PATH+set}" ]; then
  CMAKE_PREFIX_PATH="${OPT_PREFIX}/boost/current:${CMAKE_PREFIX_PATH}"
else
  CMAKE_PREFIX_PATH="${OPT_PREFIX}/boost/current"
fi
export CMAKE_PREFIX_PATH

if [ ! -f "subprojects/raygui/src/raygui.h" ]; then
  mkdir -p subprojects/raygui
  meson subprojects download raygui
  set +e
  meson subprojects update --reset raygui
  set -e
  cat <<EOF >subprojects/raygui/meson.build
project('raygui', 'c')
raygui_inc = include_directories('src', is_system: true)
raygui_dep = declare_dependency(
  include_directories: raygui_inc
)
meson.override_dependency('raygui', raygui_dep)
EOF
fi

if [ ! -d ".build" ]; then
  meson setup .build --buildtype=release
fi

export BOOST_ROOT=${OPT_PREFIX}/boost/current

meson compile -C .build

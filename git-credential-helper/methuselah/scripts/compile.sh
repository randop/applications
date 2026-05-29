#!/bin/sh
set -eu

LOCAL_PKGCONFIG=${HOME}/.local/lib/pkgconfig

if [ -n "${PKG_CONFIG_PATH+set}" ]; then
  export PKG_CONFIG_PATH="${LOCAL_PKGCONFIG}:${PKG_CONFIG_PATH}"
else
  export PKG_CONFIG_PATH=$LOCAL_PKGCONFIG
fi

if [ ! -f "subprojects/raygui/src/raygui.h" ]; then
  mkdir -p subprojects/raygui
  meson subprojects download raygui
  set +e
  meson subprojects update --reset raygui
  set -e
  cat <<EOF >subprojects/raygui/meson.build
project('raygui', 'c')
raygui_inc = include_directories('src')
raygui_dep = declare_dependency(
    include_directories: raygui_inc
)
meson.override_dependency('raygui', raygui_dep)
EOF
fi

if [ ! -d ".build" ]; then
  meson setup .build --buildtype=debug
fi

meson compile -C .build

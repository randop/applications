#!/bin/sh
set -eu

#if [ -f "subprojects/raygui/src/raygui.h" ]; then
#  meson subprojects update --reset raygui
#fi

meson compile -C .build

#!/bin/sh
# Build using Meson
meson setup builddir
meson compile -C builddir

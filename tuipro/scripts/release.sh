#!/usr/bin/env bash
#
# Build tuipro and package it as a self-contained AppImage.
#
# Usage:
#   scripts/release.sh [VERSION] [options]
#
# The version defaults to the one in CMakeLists.txt and can be overridden with
# the VERSION environment variable or as the first positional argument.
#
# The finished bundle is written to:
#   .dist/tuipro-${VERSION}.AppImage
#
# Requirements:
#   - cmake, a C compiler and make/ninja (whatever CMake picks)
#   - LuaJIT 2.1 development files (or TUIPRO_LUAJIT_ROOT as for a normal build)
#   - curl or wget to fetch appimagetool unless APPIMAGETOOL is set or found on
#     PATH. appimagetool is cached in ~/.cache/tuipro.
#
set -euo pipefail

APP=tuipro
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ICON=$ROOT/scripts/$APP.png
CACHE_DIR=${XDG_CACHE_HOME:-"${HOME:-/tmp}/.cache"}/tuipro
BUILD_DIR=$ROOT/.build/appimage
DIST_DIR=$ROOT/.dist
APPDIR=$DIST_DIR/$APP.AppDir

VERSION=${VERSION:-}
OUTPUT=${OUTPUT:-}
CLEAN=0
ALLOW_DOWNLOAD=1
SMOKE_TEST=1

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*" >&2; }
warn() { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<EOF
Build $APP and package it as an AppImage.

Usage: $(basename "$0") [VERSION] [options]

Options:
  -o, --output PATH   Output AppImage path (default: .dist/$APP-\$VERSION.AppImage)
  -c, --clean         Remove the build directory and old AppDir before building
      --no-download   Do not download appimagetool; require APPIMAGETOOL or PATH
      --no-smoke      Skip the headless smoke test of the finished AppImage
  -h, --help          Show this help

Environment:
  VERSION             Version to package (default: project version from CMakeLists.txt)
  OUTPUT              Output path (same as --output)
  APPIMAGETOOL        Path to an appimagetool binary/AppImage to use
  JOBS                Parallel build jobs (default: nproc)
  CMAKE_GENERATOR     CMake generator to use
  TUIPRO_LUAJIT_ROOT  LuaJIT source checkout, as for a normal build
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    -o|--output)
      [ $# -ge 2 ] || die "$1 requires an argument"
      OUTPUT=$2
      shift 2
      ;;
    -c|--clean)
      CLEAN=1
      shift
      ;;
    --no-download)
      ALLOW_DOWNLOAD=0
      shift
      ;;
    --no-smoke)
      SMOKE_TEST=0
      shift
      ;;
    --)
      shift
      while [ $# -gt 0 ]; do
        VERSION=$1
        shift
      done
      ;;
    -*)
      die "unknown option: $1 (try --help)"
      ;;
    *)
      VERSION=$1
      shift
      ;;
  esac
done

cd "$ROOT"

# ---------------------------------------------------------------------------
# Version
# ---------------------------------------------------------------------------
if [ -z "$VERSION" ]; then
  VERSION=$(sed -nE \
    's/^[[:space:]]*project\([[:space:]]*'"$APP"'[[:space:]]+VERSION[[:space:]]+([0-9][0-9A-Za-z._+-]*).*/\1/p' \
    "$ROOT/CMakeLists.txt" | head -n 1)
fi
VERSION=${VERSION#v}
[ -n "$VERSION" ] || die "could not determine version; pass one explicitly or set VERSION"
[[ "$VERSION" =~ ^[0-9][0-9A-Za-z._+-]*$ ]] \
  || die "invalid version '$VERSION'"

if [ -z "$OUTPUT" ]; then
  OUTPUT=$DIST_DIR/$APP-$VERSION.AppImage
fi

# ---------------------------------------------------------------------------
# Host architecture
# ---------------------------------------------------------------------------
case "$(uname -m)" in
  x86_64|amd64)  APPIMAGE_ARCH=x86_64 ;;
  aarch64|arm64) APPIMAGE_ARCH=aarch64 ;;
  armv7l|armhf)  APPIMAGE_ARCH=armhf ;;
  *) die "unsupported architecture: $(uname -m)" ;;
esac

# ---------------------------------------------------------------------------
# Tool checks
# ---------------------------------------------------------------------------
for tool in cmake strip; do
  command -v "$tool" >/dev/null 2>&1 || die "'$tool' is required but was not found"
done
command -v cc >/dev/null 2>&1 \
  || command -v gcc >/dev/null 2>&1 \
  || command -v clang >/dev/null 2>&1 \
  || die "no C compiler found (cc, gcc or clang)"
[ -f "$ICON" ] || die "missing application icon: $ICON"

if [ "$CLEAN" -eq 1 ]; then
  log "Cleaning previous build"
  rm -rf "$BUILD_DIR" "$APPDIR"
fi
rm -f "$OUTPUT"

# ---------------------------------------------------------------------------
# Build and install into the AppDir
# ---------------------------------------------------------------------------
log "Building $APP $VERSION for $APPIMAGE_ARCH"
cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib'
cmake --build "$BUILD_DIR" --parallel "${JOBS:-$(nproc 2>/dev/null || echo 2)}"

log "Installing into $APPDIR"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"
[ -x "$APPDIR/usr/bin/$APP" ] || die "install did not produce usr/bin/$APP"

# ---------------------------------------------------------------------------
# Bundle LuaJIT
#
# Everything else the binary links (libc, libm) is provided by the host.
# ---------------------------------------------------------------------------
bundle_luajit() {
  local bin=$APPDIR/usr/bin/$APP
  local needed="" path="" ldd_line

  if command -v objdump >/dev/null 2>&1; then
    needed=$(objdump -p "$bin" | awk '$1 == "NEEDED" && $2 ~ /^libluajit/ { print $2; exit }')
  fi
  ldd_line=$(ldd "$bin" 2>/dev/null | awk '/libluajit/ { print; exit }' || true)
  path=$(printf '%s\n' "$ldd_line" | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^\//) { print $i; exit } }')

  if [ -z "$needed" ] && [ -n "$ldd_line" ]; then
    needed=$(printf '%s\n' "$ldd_line" | awk '{ print $1 }')
  fi

  # Statically linked LuaJIT: nothing to bundle.
  [ -n "$needed" ] || return 0

  if [ -z "$path" ] || [ ! -f "$path" ]; then
    path=$(ldconfig -p 2>/dev/null | awk -v n="$needed" '$1 == n { print $NF; exit }')
  fi
  if [ -z "$path" ] || [ ! -f "$path" ]; then
    die "cannot locate $needed to bundle it into the AppImage"
  fi

  mkdir -p "$APPDIR/usr/lib"
  cp -L "$path" "$APPDIR/usr/lib/$needed"
  log "Bundled LuaJIT: $path -> usr/lib/$needed"
}
bundle_luajit

strip --strip-unneeded "$APPDIR/usr/bin/$APP" 2>/dev/null \
  || warn "could not strip the executable"

# ---------------------------------------------------------------------------
# AppDir metadata
# ---------------------------------------------------------------------------
log "Writing AppDir metadata"

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
set -e
HERE=$(dirname "$(readlink -f "$0")")

# Make the installed Lua tree relocatable inside the AppImage.
export TUIPRO_RUNTIME="$HERE/usr/share/tuipro"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

exec "$HERE/usr/bin/tuipro" "$@"
EOF
chmod 755 "$APPDIR/AppRun"

mkdir -p "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
         "$APPDIR/usr/share/metainfo"

cat > "$APPDIR/usr/share/applications/$APP.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=tuipro
GenericName=Terminal UI Toolkit
Comment=Terminal UI applications powered by LuaJIT
Exec=tuipro %F
Icon=tuipro
Terminal=true
Categories=Development;
Keywords=terminal;tui;lua;luajit;
StartupNotify=false
X-AppImage-Version=$VERSION
EOF

cp "$ICON" "$APPDIR/usr/share/icons/hicolor/256x256/apps/$APP.png"

cat > "$APPDIR/usr/share/metainfo/$APP.appdata.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<component type="console-application">
  <id>$APP</id>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>MIT</project_license>
  <name>tuipro</name>
  <summary>Terminal UI toolkit powered by LuaJIT</summary>
  <description>
    <p>
      tuipro is a terminal UI container with a small C core that owns the
      terminal and a diffing cell grid, while LuaJIT builds applications,
      layouts, widgets, plugins and scripts on top.
    </p>
  </description>
  <launchable type="desktop-id">$APP.desktop</launchable>
  <releases>
    <release version="$VERSION" date="$(date +%F)"/>
  </releases>
</component>
EOF

ln -sf "usr/share/applications/$APP.desktop" "$APPDIR/$APP.desktop"
ln -sf "usr/share/icons/hicolor/256x256/apps/$APP.png" "$APPDIR/$APP.png"
ln -sf "$APP.png" "$APPDIR/.DirIcon"

# ---------------------------------------------------------------------------
# appimagetool
# ---------------------------------------------------------------------------
download_appimagetool() {
  local dest=$1
  local url
  mkdir -p "$CACHE_DIR"

  for url in \
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$APPIMAGE_ARCH.AppImage" \
    "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-$APPIMAGE_ARCH.AppImage"
  do
    log "Downloading appimagetool from $url"
    if command -v curl >/dev/null 2>&1; then
      curl -fL --retry 3 --connect-timeout 20 -o "$dest.part" "$url" && break
    elif command -v wget >/dev/null 2>&1; then
      wget -q -O "$dest.part" "$url" && break
    else
      die "curl or wget is required to download appimagetool"
    fi
    rm -f "$dest.part"
  done

  [ -s "$dest.part" ] || return 1
  mv "$dest.part" "$dest"
  chmod 755 "$dest"
}

resolve_appimagetool() {
  if [ -n "${APPIMAGETOOL:-}" ]; then
    [ -x "$APPIMAGETOOL" ] || die "APPIMAGETOOL is not executable: $APPIMAGETOOL"
    printf '%s\n' "$APPIMAGETOOL"
    return
  fi

  if command -v appimagetool >/dev/null 2>&1; then
    command -v appimagetool
    return
  fi

  local cached=$CACHE_DIR/appimagetool-$APPIMAGE_ARCH.AppImage
  if [ -x "$cached" ]; then
    printf '%s\n' "$cached"
    return
  fi

  if [ "$ALLOW_DOWNLOAD" -ne 1 ]; then
    die "appimagetool not found; install it, set APPIMAGETOOL, or drop --no-download"
  fi

  download_appimagetool "$cached" || die "could not download appimagetool"
  printf '%s\n' "$cached"
}

APPIMAGETOOL_BIN=$(resolve_appimagetool)

# ---------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------
mkdir -p "$(dirname "$OUTPUT")"

log "Packaging $APP-$VERSION.AppImage"
ARCH=$APPIMAGE_ARCH VERSION=$VERSION APPIMAGE_EXTRACT_AND_RUN=1 \
  "$APPIMAGETOOL_BIN" "$APPDIR" "$OUTPUT"

[ -s "$OUTPUT" ] || die "appimagetool did not produce $OUTPUT"
chmod 755 "$OUTPUT"

# ---------------------------------------------------------------------------
# Smoke test the bundle without requiring FUSE
# ---------------------------------------------------------------------------
if [ "$SMOKE_TEST" -eq 1 ]; then
  log "Smoke-testing the AppImage"
  if ! APPIMAGE_EXTRACT_AND_RUN=1 "$OUTPUT" --headless --size 80x24 --frames 1 \
      --demo >/dev/null 2>&1; then
    die "the packaged AppImage failed its headless smoke test"
  fi
fi

log "Created $OUTPUT ($(ls -lh "$OUTPUT" | awk '{ print $5 }'))"

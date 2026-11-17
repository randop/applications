#!/bin/sh
#
# gnutls.sh
#
# Build GnuTLS (and its cryptographic dependencies) on Ubuntu 22.04 LTS
# (Jammy) from the upstream git repository.
#
# GnuTLS requires nettle >= 3.10, but Ubuntu 22.04 ships nettle 3.7.3.
# For that reason this script builds gmp, nettle, libtasn1 and libunistring
# from their official release tarballs into a private prefix first, and then
# builds GnuTLS from git against them. No system packages are installed and
# no system files are modified.
#
# Directory layout:
#
# $HOME/opt/gnutls/
#   downloads/                dependency tarballs
#   src/                      extracted dependency sources
#   gnutls-<ref>/             GnuTLS git checkout (build happens here)
#   current/                  GnuTLS installation (prefix)
#
# Usage:
#   chmod +x gnutls.sh
#   ./gnutls.sh
#
# Optional:
#   JOBS=4 ./gnutls.sh
#   JOBS=8 ./gnutls.sh
#   GNUTLS_VERSION=3.8.12 ./gnutls.sh
#
# GNUTLS_VERSION defaults to 3.8.13 and selects the matching git tag.
#
# ------------------------------------------------------------
# Copyright 2010-2026 Randolph Ledesma
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# exit on error, error on unset variables
set -eu

umask 022

# ------------------------------------------------------------
# Configuration
# ------------------------------------------------------------

GMP_VERSION="${GMP_VERSION:-6.3.0}"
NETTLE_VERSION="${NETTLE_VERSION:-3.10}"
LIBTASN1_VERSION="${LIBTASN1_VERSION:-4.19.0}"
LIBUNISTRING_VERSION="${LIBUNISTRING_VERSION:-1.1}"

GNUTLS_VERSION="${GNUTLS_VERSION:-3.8.13}"
GNUTLS_TAG="$GNUTLS_VERSION"
GNUTLS_REPO="https://github.com/gnutls/gnutls.git"

GNUTLS_ROOT="$HOME/opt/gnutls"

PREFIX="$GNUTLS_ROOT/current"

DOWNLOAD_DIR="$GNUTLS_ROOT/downloads"
SRC_DIR="$GNUTLS_ROOT/src"

GNUTLS_SRC="$GNUTLS_ROOT/gnutls-$GNUTLS_VERSION"
GNUTLS_STAMP="$GNUTLS_ROOT/.gnutls-builder-version"

GMP_URL="https://ftp.gnu.org/gnu/gmp/gmp-$GMP_VERSION.tar.xz"
NETTLE_URL="https://ftp.gnu.org/gnu/nettle/nettle-$NETTLE_VERSION.tar.gz"
LIBTASN1_URL="https://ftp.gnu.org/gnu/libtasn1/libtasn1-$LIBTASN1_VERSION.tar.gz"
LIBUNISTRING_URL="https://ftp.gnu.org/gnu/libunistring/libunistring-$LIBUNISTRING_VERSION.tar.xz"

GMP_SHA256="a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898"
NETTLE_SHA256="b4c518adb174e484cb4acea54118f02380c7133771e7e9beb98a0787194ee47c"
LIBTASN1_SHA256="1613f0ac1cf484d6ec0ce3b8c06d56263cc7242f1c23b30d82d23de345a63f7a"
LIBUNISTRING_SHA256="827c1eb9cb6e7c738b171745dac0888aa58c5924df2e59239318383de0729b98"

JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')}"

# ------------------------------------------------------------
# Helpers
# ------------------------------------------------------------

info() {
  printf '\n[INFO] %s\n' "$*"
}

warning() {
  printf '\n[WARNING] %s\n' "$*" >&2
}

fail() {
  printf '\n[ERROR] %s\n' "$*" >&2
  exit 1
}

# ------------------------------------------------------------
# Validate configuration
# ------------------------------------------------------------

[ -n "${HOME:-}" ] ||
  fail "HOME is not defined."

[ "$(id -u)" -ne 0 ] ||
  fail "Run this script as your normal user."

case "$JOBS" in
'' | *[!0-9]*)
  fail "JOBS must be a positive integer."
  ;;
esac

[ "$JOBS" -ge 1 ] ||
  fail "JOBS must be greater than zero."

printf '%s\n' "$GNUTLS_VERSION" |
  awk -F. '
        NF == 3 &&
        $1 ~ /^[0-9]+$/ &&
        $2 ~ /^[0-9]+$/ &&
        $3 ~ /^[0-9]+$/ { valid=1 }
        END { exit !valid }
    ' ||
  fail "Invalid GNUTLS_VERSION. Expected format: 3.8.13"

# ------------------------------------------------------------
# Check operating system
# ------------------------------------------------------------

[ -r /etc/os-release ] ||
  fail "Cannot identify operating system."

. /etc/os-release

[ "${ID:-}" = "ubuntu" ] ||
  fail "Ubuntu is required."

[ "${VERSION_ID:-}" = "22.04" ] ||
  fail "Ubuntu 22.04 LTS is required."

# ------------------------------------------------------------
# Dependency warning
# ------------------------------------------------------------

warning "DEPENDENCIES ARE NOT INSTALLED BY THIS SCRIPT."

cat >&2 <<'EOF'

Ensure the following build prerequisites are already available:

  BUILD TOOLS:
    GCC and G++ (host compiler)
    GNU Make
    GNU Binutils
    GNU Autoconf
    GNU Automake
    GNU Libtool
    GNU gettext (autopoint)
    GNU m4
    GNU Bison
    GNU gperf
    gtk-doc-tools (gtkdocize, gtk-doc.m4)
    pkg-config
    Perl
    Python 3
    Git
    curl
    tar
    xz
    sha256sum

  On Ubuntu 22.04 these can be installed with:

    sudo apt-get install -y \
      build-essential autoconf automake libtool gettext m4 bison \
      gperf gtk-doc-tools pkg-config perl python3 git curl \
      xz-utils

  No GMP, nettle, libtasn1 or libunistring development packages are
  required: this script builds those itself into a private prefix.

This script will not install, update, or modify system packages.

EOF

# ------------------------------------------------------------
# Check essential commands
# ------------------------------------------------------------

for tool in \
  awk \
  bash \
  cat \
  cc \
  curl \
  find \
  git \
  grep \
  make \
  mkdir \
  mv \
  perl \
  pkg-config \
  python3 \
  rm \
  sed \
  sha256sum \
  tar \
  xz; do
  command -v "$tool" >/dev/null 2>&1 ||
    fail "Required command not found: $tool"
done

for tool in \
  autoconf \
  automake \
  libtoolize \
  autopoint \
  bison \
  gperf; do
  command -v "$tool" >/dev/null 2>&1 ||
    fail "Required command not found: $tool (build prerequisite)"
done

command -v gtkdocize >/dev/null 2>&1 ||
  fail "gtkdocize not found. Install gtk-doc-tools."

# ------------------------------------------------------------
# Display configuration
# ------------------------------------------------------------

info "GnuTLS build configuration"

printf '%-22s %s\n' \
  "GnuTLS version:" "$GNUTLS_VERSION" \
  "GnuTLS root:" "$GNUTLS_ROOT" \
  "Installation:" "$PREFIX" \
  "Parallel jobs:" "$JOBS" \
  "GMP version:" "$GMP_VERSION" \
  "nettle version:" "$NETTLE_VERSION" \
  "libtasn1 version:" "$LIBTASN1_VERSION" \
  "libunistring:" "$LIBUNISTRING_VERSION"

# ------------------------------------------------------------
# Prepare directories
# ------------------------------------------------------------

mkdir -p "$GNUTLS_ROOT"

[ -w "$GNUTLS_ROOT" ] ||
  fail "GnuTLS root directory is not writable."

# Reject an unexpected symlink for the installation directory.
[ ! -L "$PREFIX" ] ||
  fail "$PREFIX must be a directory, not a symbolic link."

mkdir -p "$PREFIX" "$DOWNLOAD_DIR" "$SRC_DIR"

[ -w "$PREFIX" ] ||
  fail "Installation directory is not writable: $PREFIX"

# All packages are built into, and discovered from, the private prefix.
# $PREFIX/bin is on PATH so that GnuTLS's build can run tools installed by the
# dependencies (notably libtasn1's asn1Parser, which generates the ASN.1 tables).
export PATH="$PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CPPFLAGS="-I$PREFIX/include${CPPFLAGS:+ $CPPFLAGS}"
export LDFLAGS="-L$PREFIX/lib -Wl,-rpath,$PREFIX/lib${LDFLAGS:+ $LDFLAGS}"

# Every dependency is configured with an explicit --libdir="$PREFIX/lib".
# Without it, nettle's configure detects a 64-bit ABI and installs its
# libraries and pkg-config files into $PREFIX/lib64, which this script and
# GnuTLS never look at. Pinning --libdir keeps everything in $PREFIX/lib.

# GCC 15 and later default to -std=gnu23 (C23). The older C dependencies
# (GMP, nettle, libtasn1, libunistring) rely on pre-C23 semantics and fail to
# build with that default. Pin a pre-C23 standard so the build works with both
# the Ubuntu 22.04 system compiler and a newer GCC, unless the caller already
# selected a language standard through CFLAGS.
if [ -z "${CFLAGS:-}" ]; then
  CFLAGS="-O2"
fi

case " $CFLAGS " in
*" -std="*) ;;
*) CFLAGS="$CFLAGS -std=gnu17" ;;
esac

export CFLAGS

# ------------------------------------------------------------
# Helpers: download, checksum, dependency detection
# ------------------------------------------------------------

download_file() {
  download_url=$1
  download_destination=$2

  download_tmp="$download_destination.tmp.$$"

  rm -f "$download_tmp"

  curl \
    --fail \
    --location \
    --show-error \
    --retry 3 \
    --retry-delay 2 \
    --output "$download_tmp" \
    "$download_url" ||
    {
      rm -f "$download_tmp"
      fail "Download failed: $download_url"
    }

  [ -s "$download_tmp" ] ||
    {
      rm -f "$download_tmp"
      fail "Downloaded file is empty: $download_url"
    }

  mv -f "$download_tmp" "$download_destination"
}

verify_sha256() {
  verify_file=$1
  verify_expected=$2

  verify_actual=$(
    sha256sum "$verify_file" | awk '{ print $1 }'
  )

  [ "$verify_expected" = "$verify_actual" ] ||
    fail "SHA-256 verification failed for $verify_file"

  info "SHA-256 verified: $(basename "$verify_file")"
}

# Download, verify and extract a dependency tarball.
# Sets PREPARED_SRC to the extracted source directory.
prepare_source() {
  prepare_name=$1
  prepare_version=$2
  prepare_url=$3
  prepare_sha256=$4

  prepare_tarball="$DOWNLOAD_DIR/$(basename "$prepare_url")"
  PREPARED_SRC="$SRC_DIR/$prepare_name-$prepare_version"

  if [ ! -s "$prepare_tarball" ]; then
    info "Downloading $prepare_name $prepare_version..."
    download_file "$prepare_url" "$prepare_tarball"
  else
    info "Using existing source archive: $(basename "$prepare_tarball")"
  fi

  verify_sha256 "$prepare_tarball" "$prepare_sha256"

  # Always extract into a clean tree. Reusing a partially built source
  # directory from an earlier failed run can leave stale object files or a
  # half-configured tree that makes `make` fail in confusing ways.
  info "Extracting $prepare_name $prepare_version..."
  rm -rf "$PREPARED_SRC"
  tar -xf "$prepare_tarball" -C "$SRC_DIR"

  [ -x "$PREPARED_SRC/configure" ] ||
    fail "$prepare_name configure script not found in $PREPARED_SRC"
}

# Print the Version field of a pkg-config file inside the private prefix.
# Reading the file directly (instead of asking the global pkg-config) means we
# always inspect the library this script built, never an older system copy that
# happens to be on the default pkg-config search path.
pc_file_version() {
  pc_file_version_path="$PREFIX/lib/pkgconfig/$1.pc"

  [ -f "$pc_file_version_path" ] || return 1

  sed -n 's/^Version:[[:space:]]*//p' "$pc_file_version_path" | sed -n '1p'
}

# True (0) when a package with the exact version is already installed
# in the private prefix.
dep_installed() {
  dep_pkg=$1
  dep_version=$2

  [ "$(pc_file_version "$dep_pkg" 2>/dev/null || true)" = "$dep_version" ]
}

# Fail with a precise diagnostic when a package is missing or has the wrong
# version. This catches both failure modes: a dependency that never built
# (missing .pc) and an older system library shadowing the private one.
require_pkg_version() {
  require_name=$1
  require_expected=$2
  require_pc="$PREFIX/lib/pkgconfig/$require_name.pc"

  if [ ! -f "$require_pc" ]; then
    fail "Missing pkg-config file in private prefix: $require_pc" \
      "(expected $require_name $require_expected). The dependency did not build or install."
  fi

  require_actual=$(pc_file_version "$require_name" 2>/dev/null || true)

  [ "$require_actual" = "$require_expected" ] ||
    fail "Wrong version for $require_name in $require_pc: expected $require_expected, found ${require_actual:-<none>}"
}

# ------------------------------------------------------------
# Build GMP
# ------------------------------------------------------------

if dep_installed gmp "$GMP_VERSION"; then
  info "GMP $GMP_VERSION already installed."
else
  prepare_source gmp "$GMP_VERSION" "$GMP_URL" "$GMP_SHA256"
  gmp_src="$PREPARED_SRC"

  info "Building GMP $GMP_VERSION..."

  # NOTE: failures must be joined with && inside the subshell. With `set -e`,
  # a failing command in the middle of a `( ...; ...; ... )` sequence is
  # silently ignored, because -e is disabled on the left side of `||`.
  (
    cd "$gmp_src" &&
      ./configure \
        --prefix="$PREFIX" \
        --libdir="$PREFIX/lib" \
        --enable-shared \
        --disable-static &&
      make -j"$JOBS" &&
      make install
  ) || fail "GMP build failed."
fi

# ------------------------------------------------------------
# Build nettle
# ------------------------------------------------------------

if dep_installed nettle "$NETTLE_VERSION" &&
  dep_installed hogweed "$NETTLE_VERSION"; then
  info "nettle $NETTLE_VERSION already installed."
else
  prepare_source nettle "$NETTLE_VERSION" "$NETTLE_URL" "$NETTLE_SHA256"
  nettle_src="$PREPARED_SRC"

  info "Building nettle $NETTLE_VERSION..."

  (
    cd "$nettle_src" &&
      ./configure \
        --prefix="$PREFIX" \
        --libdir="$PREFIX/lib" \
        --disable-static \
        --disable-documentation \
        --disable-openssl &&
      make -j"$JOBS" &&
      make install
  ) || fail "nettle build failed."
fi

# ------------------------------------------------------------
# Build libtasn1
# ------------------------------------------------------------

if dep_installed libtasn1 "$LIBTASN1_VERSION"; then
  info "libtasn1 $LIBTASN1_VERSION already installed."
else
  prepare_source libtasn1 "$LIBTASN1_VERSION" "$LIBTASN1_URL" "$LIBTASN1_SHA256"
  libtasn1_src="$PREPARED_SRC"

  info "Building libtasn1 $LIBTASN1_VERSION..."

  (
    cd "$libtasn1_src" &&
      ./configure \
        --prefix="$PREFIX" \
        --libdir="$PREFIX/lib" \
        --disable-static \
        --disable-doc &&
      make -j"$JOBS" &&
      make install
  ) || fail "libtasn1 build failed."
fi

# ------------------------------------------------------------
# Build libunistring
# ------------------------------------------------------------

if [ -f "$PREFIX/lib/libunistring.so" ] &&
  [ -f "$PREFIX/include/unistr.h" ]; then
  info "libunistring $LIBUNISTRING_VERSION already installed."
else
  prepare_source libunistring "$LIBUNISTRING_VERSION" "$LIBUNISTRING_URL" "$LIBUNISTRING_SHA256"
  libunistring_src="$PREPARED_SRC"

  info "Building libunistring $LIBUNISTRING_VERSION..."

  (
    cd "$libunistring_src" &&
      ./configure \
        --prefix="$PREFIX" \
        --libdir="$PREFIX/lib" \
        --disable-static &&
      make -j"$JOBS" &&
      make install
  ) || fail "libunistring build failed."
fi

# Every dependency must be visible, at the exact version we built, before
# configuring GnuTLS. A version mismatch here means the freshly built library
# is being shadowed by an older system copy, or a build step failed silently.
require_pkg_version gmp "$GMP_VERSION"
require_pkg_version nettle "$NETTLE_VERSION"
require_pkg_version hogweed "$NETTLE_VERSION"
require_pkg_version libtasn1 "$LIBTASN1_VERSION"

# libunistring does not install a pkg-config file; GnuTLS links it with -lunistring.
[ -f "$PREFIX/lib/libunistring.so" ] ||
  fail "Required dependency not found: $PREFIX/lib/libunistring.so"
[ -f "$PREFIX/include/unistr.h" ] ||
  fail "Required dependency not found: $PREFIX/include/unistr.h"

# ------------------------------------------------------------
# Fetch GnuTLS source
# ------------------------------------------------------------

if [ -x "$PREFIX/bin/certtool" ] &&
  [ -f "$GNUTLS_STAMP" ] &&
  [ "$(cat "$GNUTLS_STAMP")" = "$GNUTLS_VERSION" ]; then

  info "GnuTLS '$GNUTLS_VERSION' already built and installed."

else

  if [ -d "$GNUTLS_SRC/.git" ]; then
    info "Reusing existing GnuTLS checkout: $GNUTLS_SRC"
  else
    info "Cloning GnuTLS $GNUTLS_VERSION..."

    rm -rf "$GNUTLS_SRC"

    git clone \
      --branch "$GNUTLS_TAG" \
      --single-branch \
      "$GNUTLS_REPO" \
      "$GNUTLS_SRC" ||
      fail "Failed to clone GnuTLS $GNUTLS_VERSION from $GNUTLS_REPO"
  fi

  # ----------------------------------------------------------
  # Bootstrap GnuTLS (gnulib, submodules, autoreconf)
  # ----------------------------------------------------------
  #
  # This requires network access: gnulib and the git submodules are
  # fetched here. It requires gperf and gtk-doc-tools.

  info "Bootstrapping GnuTLS..."

  (
    cd "$GNUTLS_SRC" &&
      ./bootstrap
  ) || fail "GnuTLS bootstrap failed."

  # ----------------------------------------------------------
  # Configure GnuTLS
  # ----------------------------------------------------------

  info "Configuring GnuTLS..."

  # Make sure configure sees the freshly built dependencies and not any older
  # system copy of them. PKG_CONFIG_PATH is already exported above, but we set
  # it explicitly here as well so the intent is unambiguous.
  (
    cd "$GNUTLS_SRC" &&
      PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
        ./configure \
        --prefix="$PREFIX" \
        --libdir="$PREFIX/lib" \
        --enable-shared \
        --disable-static \
        --disable-doc \
        --disable-tests \
        --disable-full-test-suite \
        --without-p11-kit \
        --without-tpm \
        --disable-libdane \
        --disable-gcc-warnings \
        --disable-maintainer-mode
  ) || fail "GnuTLS configure failed."

  # ----------------------------------------------------------
  # Build and install GnuTLS
  # ----------------------------------------------------------

  info "Building GnuTLS with -j$JOBS..."

  (
    cd "$GNUTLS_SRC" &&
      make -j"$JOBS"
  ) || fail "GnuTLS build failed."

  info "Installing GnuTLS into $PREFIX..."

  (
    cd "$GNUTLS_SRC" &&
      make install
  ) || fail "GnuTLS install failed."

  printf '%s\n' "$GNUTLS_VERSION" >"$GNUTLS_STAMP"

fi

# ------------------------------------------------------------
# Create GnuTLS activation script
# ------------------------------------------------------------

info "Creating GnuTLS activation script..."

{
  printf '%s\n' \
    '# GnuTLS environment activation'

  printf 'export PATH="%s/bin:$PATH"\n' "$PREFIX"

  printf 'export LD_LIBRARY_PATH="%s/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n' \
    "$PREFIX"

  printf 'export PKG_CONFIG_PATH="%s/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"\n' \
    "$PREFIX"

} >"$PREFIX/enable.sh"

# ------------------------------------------------------------
# Expose gnutls.pc to the user-local pkg-config directory
# ------------------------------------------------------------

LOCAL_PKGCONFIG="$HOME/.local/lib/pkgconfig"

mkdir -p "$LOCAL_PKGCONFIG"

if [ ! -e "$LOCAL_PKGCONFIG/gnutls.pc" ]; then
  ln -sv "$PREFIX/lib/pkgconfig/gnutls.pc" "$LOCAL_PKGCONFIG/gnutls.pc"
fi

# ------------------------------------------------------------
# Verify installed GnuTLS
# ------------------------------------------------------------

info "Verifying GnuTLS installation..."

for tool in certtool gnutls-cli gnutls-serv psktool; do

  TOOL_PATH="$PREFIX/bin/$tool"

  [ -x "$TOOL_PATH" ] ||
    fail "Missing installed tool: $TOOL_PATH"

done

[ -f "$PREFIX/lib/libgnutls.so" ] ||
  fail "Missing shared library: $PREFIX/lib/libgnutls.so"

"$PREFIX/bin/certtool" --version | sed -n '1p'
"$PREFIX/bin/gnutls-cli" --version | sed -n '1p'

# ------------------------------------------------------------
# Create and run smoke test
# ------------------------------------------------------------

cat >"$GNUTLS_ROOT/smoke.c" <<'EOF_SMOKE'
#include <gnutls/gnutls.h>
#include <stdio.h>

int main(void)
{
    gnutls_global_init();

    const char *version = gnutls_check_version(NULL);

    printf("GnuTLS library version: %s\n", version ? version : "unknown");

    gnutls_global_deinit();

    return 0;
}
EOF_SMOKE

info "Compiling and running GnuTLS smoke test..."

cc \
  -O2 \
  $(pkg-config --cflags gnutls) \
  "$GNUTLS_ROOT/smoke.c" \
  -o "$GNUTLS_ROOT/smoke" \
  $(pkg-config --libs gnutls) ||
  fail "GnuTLS smoke test compilation failed."

LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  "$GNUTLS_ROOT/smoke" ||
  fail "GnuTLS smoke test failed."

# ------------------------------------------------------------
# Build summary
# ------------------------------------------------------------

cat <<EOF

============================================================
 GnuTLS $GNUTLS_VERSION BUILD SUCCESSFUL
============================================================

SOURCE:
    $GNUTLS_SRC

DOWNLOAD:
    $DOWNLOAD_DIR

INSTALLATION:
    $PREFIX

LIBRARY:
    $PREFIX/lib/libgnutls.so

TOOLS:
    $PREFIX/bin/certtool
    $PREFIX/bin/gnutls-cli
    $PREFIX/bin/gnutls-serv
    $PREFIX/bin/psktool

PKG-CONFIG:
    $PREFIX/lib/pkgconfig/gnutls.pc

ACTIVATE:
    . "$PREFIX/enable.sh"

VERIFY:
    certtool --version
    gnutls-cli --version

BUILD:
    Shared library, no tests, no docs.
    C standard: gnu17 (pre-C23, for GCC 15+ compatibility).
    PKCS#11, TPM and DANE disabled.

DEPENDENCIES:
    gmp $GMP_VERSION
    nettle $NETTLE_VERSION
    libtasn1 $LIBTASN1_VERSION
    libunistring $LIBUNISTRING_VERSION

SYSTEM:
    No root access used.
    No system packages installed.
    No system libraries replaced.

============================================================

EOF
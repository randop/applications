#!/bin/sh
#
# gcc-builder.sh
#
# Build optimized GCC on Ubuntu 22.04 LTS (Jammy).
#
# Directory layout:
#
# $HOME/opt/gcc/
#   $GCC_BUILD_VERSION/       GCC source
#   build/                    GCC build artifacts
#   gcc-$GCC_BUILD_VERSION.tar.xz
#   sha512.sum
#   current/                  GCC installation
#
# Usage:
#   chmod +x gcc-builder.sh
#   ./gcc-builder.sh
#
# Optional:
#   JOBS=4 ./gcc-builder.sh
#   JOBS=8 ./gcc-builder.sh
#   GCC_BUILD_VERSION=16.2.0 JOBS=8 ./gcc-builder.sh
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

GCC_BUILD_VERSION="${GCC_BUILD_VERSION:-16.2.0}"

GCC_ROOT="$HOME/opt/gcc"

SOURCE_DIR="$GCC_ROOT/$GCC_BUILD_VERSION"
BUILD_DIR="$GCC_ROOT/build"
PREFIX="$GCC_ROOT/current"

TARBALL_NAME="gcc-$GCC_BUILD_VERSION.tar.xz"
TARBALL="$GCC_ROOT/$TARBALL_NAME"

CHECKSUM_FILE="$GCC_ROOT/sha512.sum"

BASE_URL="https://gcc.gnu.org/pub/gcc/releases/gcc-$GCC_BUILD_VERSION"

BUILD_CONFIG="bootstrap-O3 bootstrap-native bootstrap-lto-lean"
BUILD_TARGET="profiledbootstrap"

JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')}"

BUILD_STAMP="$BUILD_DIR/.gcc-builder-version"

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

printf '%s\n' "$GCC_BUILD_VERSION" |
  awk -F. '
        NF == 3 &&
        $1 ~ /^[0-9]+$/ &&
        $2 ~ /^[0-9]+$/ &&
        $3 ~ /^[0-9]+$/ { valid=1 }
        END { exit !valid }
    ' ||
  fail "Invalid GCC_BUILD_VERSION. Expected format: 16.2.0"

case "$JOBS" in
'' | *[!0-9]*)
  fail "JOBS must be a positive integer."
  ;;
esac

[ "$JOBS" -ge 1 ] ||
  fail "JOBS must be greater than zero."

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
    GCC and G++ (host compiler with C++14 support)
    GNU Make
    GNU Binutils
    Bash
    Flex
    GNU Awk
    Perl
    Python 3
    curl
    tar
    xz
    sha512sum

  DEVELOPMENT LIBRARIES:
    GMP
    MPFR
    MPC
    zlib
    zstd
    ISL (optional; required for Graphite optimizations)

  LTO:
    Binutils with linker-plugin support.

Required development headers and libraries must be discoverable
by the compiler and linker.

This script will not install, update, or modify system packages.

EOF

# ------------------------------------------------------------
# Check essential commands
# ------------------------------------------------------------

for tool in \
  awk \
  bash \
  cat \
  curl \
  find \
  gcc \
  g++ \
  getconf \
  id \
  make \
  mkdir \
  mv \
  rm \
  sed \
  sha512sum \
  tar \
  xz; do
  command -v "$tool" >/dev/null 2>&1 ||
    fail "Required command not found: $tool"
done

command -v cc >/dev/null 2>&1 ||
  fail "A C compiler is required."

command -v c++ >/dev/null 2>&1 ||
  fail "A C++ compiler is required."

# GCC requires a sufficiently recent bootstrap compiler.
printf '%s\n' 'int main() { return 0; }' |
  g++ -std=c++14 -x c++ -fsyntax-only - ||
  fail "The host G++ compiler must support C++14."

CONFIG_SHELL=/bin/bash
[ -x "$CONFIG_SHELL" ] ||
  fail "Bash is required at /bin/bash."

export CONFIG_SHELL

# ------------------------------------------------------------
# Display configuration
# ------------------------------------------------------------

info "GCC build configuration"

printf '%-22s %s\n' \
  "GCC version:" "$GCC_BUILD_VERSION" \
  "GCC root:" "$GCC_ROOT" \
  "Source:" "$SOURCE_DIR" \
  "Build:" "$BUILD_DIR" \
  "Installation:" "$PREFIX" \
  "Parallel jobs:" "$JOBS" \
  "Build configuration:" "$BUILD_CONFIG" \
  "Build target:" "$BUILD_TARGET"

# ------------------------------------------------------------
# Prepare directories
# ------------------------------------------------------------

mkdir -p "$GCC_ROOT"

[ -w "$GCC_ROOT" ] ||
  fail "GCC root directory is not writable."

# Reject an unexpected symlink for the installation directory.
[ ! -L "$PREFIX" ] ||
  fail "$PREFIX must be a directory, not a symbolic link."

mkdir -p "$PREFIX"

[ -w "$PREFIX" ] ||
  fail "Installation directory is not writable: $PREFIX"

# ------------------------------------------------------------
# Download helper
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
    "$download_url"

  [ -s "$download_tmp" ] ||
    fail "Downloaded file is empty: $download_url"

  mv -f "$download_tmp" "$download_destination"
}

# ------------------------------------------------------------
# Download GCC source
# ------------------------------------------------------------

if [ ! -s "$TARBALL" ]; then

  info "Downloading GCC $GCC_BUILD_VERSION source..."

  download_file \
    "$BASE_URL/$TARBALL_NAME" \
    "$TARBALL"

else

  info "Using existing source archive: $TARBALL"

fi

# ------------------------------------------------------------
# Download official checksum list
# ------------------------------------------------------------

info "Downloading official GCC SHA-512 checksums..."

download_file \
  "$BASE_URL/sha512.sum" \
  "$CHECKSUM_FILE"

# ------------------------------------------------------------
# Verify SHA-512
# ------------------------------------------------------------

info "Verifying GCC source archive..."

EXPECTED_SHA512=$(
  awk -v filename="$TARBALL_NAME" \
    '$2 == filename { print $1; exit }' \
    "$CHECKSUM_FILE"
)

[ -n "$EXPECTED_SHA512" ] ||
  fail "GCC SHA-512 checksum not found."

case "$EXPECTED_SHA512" in
*[!0123456789abcdefABCDEF]*)
  fail "Invalid SHA-512 checksum."
  ;;
esac

[ "${#EXPECTED_SHA512}" -eq 128 ] ||
  fail "Invalid SHA-512 checksum length."

ACTUAL_SHA512=$(
  sha512sum "$TARBALL" | awk '{ print $1 }'
)

[ "$EXPECTED_SHA512" = "$ACTUAL_SHA512" ] ||
  fail "GCC source archive SHA-512 verification failed."

info "SHA-512 verification successful."

xz -t "$TARBALL" ||
  fail "GCC source archive integrity check failed."

# ------------------------------------------------------------
# Extract GCC source
# ------------------------------------------------------------

if [ -f "$SOURCE_DIR/gcc/BASE-VER" ]; then

  SOURCE_VERSION=$(
    sed -n '1p' "$SOURCE_DIR/gcc/BASE-VER" |
      tr -d '\r'
  )

  [ "$SOURCE_VERSION" = "$GCC_BUILD_VERSION" ] ||
    fail "Existing source version mismatch."

  info "GCC source already extracted."

else

  if [ -d "$SOURCE_DIR" ]; then

    EXISTING_FILES=$(
      find "$SOURCE_DIR" \
        -mindepth 1 \
        -maxdepth 1 \
        -print -quit
    )

    [ -z "$EXISTING_FILES" ] ||
      fail "Source directory is incomplete. Inspect or remove $SOURCE_DIR."

  fi

  info "Extracting GCC source directly into $SOURCE_DIR..."

  mkdir -p "$SOURCE_DIR"

  tar -xf "$TARBALL" \
    --strip-components=1 \
    -C "$SOURCE_DIR"

fi

[ -x "$SOURCE_DIR/configure" ] ||
  fail "GCC configure script not found."

[ -f "$SOURCE_DIR/gcc/BASE-VER" ] ||
  fail "GCC version marker not found."

SOURCE_VERSION=$(
  sed -n '1p' "$SOURCE_DIR/gcc/BASE-VER" |
    tr -d '\r'
)

[ "$SOURCE_VERSION" = "$GCC_BUILD_VERSION" ] ||
  fail "Extracted GCC version mismatch."

# ------------------------------------------------------------
# Download GCC build prerequisites
# ------------------------------------------------------------
#
# Downloads:
#   GMP
#   MPFR
#   MPC
#   ISL
#   gettext
#
# Downloads and extracted dependency sources go to:
#   $HOME/opt/gcc/
#
# Small symlinks are created in:
#   $HOME/opt/gcc/$GCC_BUILD_VERSION/
#
# All prerequisite checksums are verified using the
# checksum manifest shipped with the GCC source release.
#
# No root privileges. No apt. No system modifications.
# ------------------------------------------------------------

info "Preparing GCC build prerequisites..."

[ -x "$SOURCE_DIR/contrib/download_prerequisites" ] ||
  fail "GCC download_prerequisites script not found."

(
  cd "$SOURCE_DIR"

  /bin/sh ./contrib/download_prerequisites \
    --directory="$GCC_ROOT" \
    --verify \
    --sha512
) || fail "Failed to download or verify GCC prerequisites."

info "GMP, MPFR, MPC, ISL and gettext are ready."

# ------------------------------------------------------------
# Prepare shared build directory
# ------------------------------------------------------------
#
# The build directory is shared between GCC releases.
# Reset it when changing GCC_BUILD_VERSION or when a previous
# configure/build failed.
#
# Source files are never stored inside this directory.
#

CONFIGURED=0

if [ -f "$BUILD_DIR/Makefile" ] &&
  [ -f "$BUILD_STAMP" ] &&
  [ "$(cat "$BUILD_STAMP")" = "$GCC_BUILD_VERSION" ]; then

  CONFIGURED=1

else

  if [ -d "$BUILD_DIR" ]; then

    info "Preparing clean build directory..."

    rm -rf "$BUILD_DIR"

  fi

fi

mkdir -p "$BUILD_DIR"

# ------------------------------------------------------------
# Configure GCC
# ------------------------------------------------------------

if [ "$CONFIGURED" -eq 0 ]; then

  info "Configuring GCC $GCC_BUILD_VERSION..."

  (
    cd "$BUILD_DIR"

    "$SOURCE_DIR/configure" \
      --prefix="$PREFIX" \
      --enable-languages=c,c++,fortran \
      --enable-bootstrap \
      --enable-lto \
      --enable-threads=posix \
      --enable-shared \
      --enable-checking=release \
      --disable-multilib \
      --disable-werror \
      --with-system-zlib
  )

  printf '%s\n' "$GCC_BUILD_VERSION" >"$BUILD_STAMP"

else

  info "Reusing existing GCC configuration."

fi

# ------------------------------------------------------------
# Optimized GCC PGO bootstrap
# ------------------------------------------------------------
#
# bootstrap-O3:
#   Optimize GCC bootstrap compiler with -O3.
#
# bootstrap-native:
#   Optimize GCC compiler binaries for the build CPU.
#
# bootstrap-lto-lean:
#   Enable LTO for the final bootstrap stage.
#
# profiledbootstrap:
#   Profile-guided optimization of the GCC compiler itself.
#
# These options optimize GCC's own compiler executables.
# They do not make -march=native the default for generated
# application binaries.
#

info "Building GCC with optimized PGO bootstrap..."

if ! make \
  -C "$BUILD_DIR" \
  -j"$JOBS" \
  BUILD_CONFIG="$BUILD_CONFIG" \
  "$BUILD_TARGET"; then

  rm -f "$BUILD_STAMP"

  fail "GCC bootstrap failed. The build directory will be reset on the next run."

fi

# ------------------------------------------------------------
# Install GCC
# ------------------------------------------------------------

info "Installing GCC into $PREFIX..."

# Entirely user-local.
# No root access or system-wide modifications.

make -C "$BUILD_DIR" install

# ------------------------------------------------------------
# Discover GCC runtime library directories
# ------------------------------------------------------------

RUNTIME_DIRS=""

add_runtime_dir() {
  runtime_path=$(
    "$1" "-print-file-name=$2"
  )

  case "$runtime_path" in
  */*)
    if [ -f "$runtime_path" ]; then

      runtime_dir=$(dirname "$runtime_path")

      case ":$RUNTIME_DIRS:" in
      *":$runtime_dir:"*) ;;
      *)
        if [ -n "$RUNTIME_DIRS" ]; then
          RUNTIME_DIRS="$RUNTIME_DIRS:$runtime_dir"
        else
          RUNTIME_DIRS="$runtime_dir"
        fi
        ;;
      esac

    fi
    ;;
  esac
}

add_runtime_dir "$PREFIX/bin/g++" libstdc++.so.6
add_runtime_dir "$PREFIX/bin/gcc" libgcc_s.so.1
add_runtime_dir "$PREFIX/bin/gfortran" libgfortran.so.5
add_runtime_dir "$PREFIX/bin/gfortran" libquadmath.so.0

# ------------------------------------------------------------
# Create GCC activation script
# ------------------------------------------------------------

info "Creating GCC activation script..."

{
  printf '%s\n' \
    '# GCC environment activation'

  printf 'export PATH="%s/bin:$PATH"\n' "$PREFIX"

  if [ -n "$RUNTIME_DIRS" ]; then

    printf 'export LD_LIBRARY_PATH="%s${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n' \
      "$RUNTIME_DIRS"

  fi

} >"$PREFIX/enable.sh"

# ------------------------------------------------------------
# Verify installed GCC
# ------------------------------------------------------------

info "Verifying GCC installation..."

for compiler in gcc g++ gfortran; do

  COMPILER_PATH="$PREFIX/bin/$compiler"

  [ -x "$COMPILER_PATH" ] ||
    fail "Missing installed compiler: $COMPILER_PATH"

  INSTALLED_VERSION=$(
    "$COMPILER_PATH" -dumpfullversion -dumpversion
  )

  [ "$INSTALLED_VERSION" = "$GCC_BUILD_VERSION" ] ||
    fail "$compiler version mismatch: $INSTALLED_VERSION"

  "$COMPILER_PATH" --version | sed -n '1p'

done

# ------------------------------------------------------------
# Create smoke tests
# ------------------------------------------------------------

cat >"$BUILD_DIR/test.c" <<'EOF_C'
int main(void)
{
    return 0;
}
EOF_C

cat >"$BUILD_DIR/test.cpp" <<'EOF_CPP'
#include <iostream>
#include <string>

int main()
{
    std::string message = "GCC C++ smoke test passed";

    std::cout << message << '\n';

    return 0;
}
EOF_CPP

cat >"$BUILD_DIR/test.f90" <<'EOF_FORTRAN'
program gcc_smoke_test

    print *, "GCC Fortran smoke test passed"

end program gcc_smoke_test
EOF_FORTRAN

# ------------------------------------------------------------
# Compile smoke tests
# ------------------------------------------------------------

info "Compiling C, C++, and Fortran tests..."

"$PREFIX/bin/gcc" \
  -O3 \
  -march=native \
  -mtune=native \
  -flto=auto \
  -pipe \
  "$BUILD_DIR/test.c" \
  -o "$BUILD_DIR/test-c"

"$PREFIX/bin/g++" \
  -O3 \
  -march=native \
  -mtune=native \
  -flto=auto \
  -pipe \
  -std=c++20 \
  "$BUILD_DIR/test.cpp" \
  -o "$BUILD_DIR/test-cpp"

"$PREFIX/bin/gfortran" \
  -O3 \
  -march=native \
  -mtune=native \
  -flto=auto \
  -pipe \
  "$BUILD_DIR/test.f90" \
  -o "$BUILD_DIR/test-fortran"

# ------------------------------------------------------------
# Run smoke tests
# ------------------------------------------------------------

run_test() {
  if [ -n "$RUNTIME_DIRS" ]; then

    LD_LIBRARY_PATH="$RUNTIME_DIRS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
      "$@"

  else

    "$@"

  fi
}

info "Running compiler tests..."

run_test "$BUILD_DIR/test-c"
run_test "$BUILD_DIR/test-cpp"
run_test "$BUILD_DIR/test-fortran"

# ------------------------------------------------------------
# Build summary
# ------------------------------------------------------------

cat <<EOF

============================================================
 GCC $GCC_BUILD_VERSION BUILD SUCCESSFUL
============================================================

SOURCE:
    $SOURCE_DIR

BUILD:
    $BUILD_DIR

DOWNLOAD:
    $TARBALL

CHECKSUM:
    $CHECKSUM_FILE

INSTALLATION:
    $PREFIX

COMPILERS:
    $PREFIX/bin/gcc
    $PREFIX/bin/g++
    $PREFIX/bin/gfortran

ACTIVATE:
    . "$PREFIX/enable.sh"

VERIFY:
    gcc --version
    g++ --version
    gfortran --version

BUILD:
    PGO + O3 + NATIVE + LTO

SYSTEM:
    No root access used.
    No system packages installed.
    No system GCC replaced.
    No system linker configuration changed.

============================================================

EOF

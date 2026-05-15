#!/bin/sh
# -*- mode: shell-script; indent-tabs-mode: nil; sh-basic-offset: 4; -*-
# ex: ts=8 sw=4 sts=4 et filetype=sh
#
#  tigerbeetle/scripts/setup.sh
#
#  Copyright © 2010 — 2026 Randolph Ledesma
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

set -eu

# Configuration
ZIG_VERSION="0.14.1"
TB_VERSION="0.17.4"
TB_SOURCE="${HOME}/opt/tigerbeetle/${TB_VERSION}"
TB_INSTALL="${HOME}/opt/tigerbeetle/current"
ZIG_DIR="${HOME}/opt/zig"
ZIG_CURRENT="${ZIG_DIR}/current"
DATA_FILE="${HOME}/projects/0_0.tigerbeetle"

echo "Downloading TigerBeetle ${TB_VERSION} ..."

# Setup Zig
if [ ! -f "${ZIG_CURRENT}/bin/zig" ]; then
    echo "Downloading Zig ${ZIG_VERSION}..."
    mkdir -p "${HOME}/opt/zig"
    cd "${HOME}/opt/zig"

    wget -q "https://ziglang.org/download/${ZIG_VERSION}/zig-x86_64-linux-${ZIG_VERSION}.tar.xz" -O zig.tar.xz
    tar -xf zig.tar.xz
    rm zig.tar.xz
    ln -sfn "$HOME/opt/zig/zig-x86_64-linux-${ZIG_VERSION}" "${ZIG_CURRENT}"
    echo "Zig ${ZIG_VERSION} installed"
else
    echo "Zig already installed"
fi

ZIG="${ZIG_CURRENT}/zig"

# Clone TigerBeetle
if [ ! -d "${TB_SOURCE}" ]; then
    mkdir -p "$(dirname "${TB_SOURCE}")"
    git clone --branch "${TB_VERSION}" \
        https://github.com/tigerbeetle/tigerbeetle.git "${TB_SOURCE}"
fi

cd "${TB_SOURCE}"

# Apply necessary patches
echo "Applying build patches..."
sed -i 's/"x86_64_v3+aes"/"baseline+aes"/' build.zig
sed -i 's/assert(std.crypto.core.aes.has_hardware_support);/\/\/ assert(std.crypto.core.aes.has_hardware_support);/' \
    src/vsr/checksum.zig

# Build
echo "Building TigerBeetle..."
"${ZIG}" build -Dtarget=x86_64-linux -Drelease -p "${TB_INSTALL}"

echo "TigerBeetle build: DONE"

# Format data file
mkdir -p "${HOME}/projects"
if [ ! -f "${DATA_FILE}" ]; then
    echo "Formatting data file..."
    cd "${TB_INSTALL}"
    ./bin/tigerbeetle format \
        --cluster=0 \
        --replica=0 \
        --replica-count=1 \
        --development \
        "${DATA_FILE}"
fi

# Check
$HOME/opt/tigerbeetle/current/bin/tigerbeetle version
# TigerBeetle version 65535.0.0+c93615a

# Done
cat <<EOF

=== Setup: DONE ===

Start server:
    cd ${TB_INSTALL}
    ./bin/tigerbeetle start --addresses=3000 --development ${DATA_FILE}

Open REPL:
    cd ${TB_INSTALL}
    ./bin/tigerbeetle repl --cluster=0 --addresses=3000
EOF

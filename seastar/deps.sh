#!/bin/sh

set -e

CARES_VERSION=v1.34.6
mkdir -p /opt/c-ares/current
rm -rf /opt/c-ares/${CARES_VERSION}
git clone -b ${CARES_VERSION} https://github.com/c-ares/c-ares.git /opt/c-ares/${CARES_VERSION}
rm -rf /opt/c-ares/${CARES_VERSION}/.git
rm -rf /opt/c-ares/${CARES_VERSION}/.github
cd /opt/c-ares/${CARES_VERSION}
autoreconf -fi
./configure --prefix=/opt/c-ares/current
make -j$(nproc)
make install
echo "Checking compiled c-ares library..." && file /opt/c-ares/current/lib/libcares.a

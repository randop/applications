#!/bin/sh

set -e

# TODO: ln -sv /opt/c-ares/current/lib/pkgconfig/libcares.pc /usr/lib/pkgconfig/libcares.pc
CARES_VERSION=v1.34.6
if [ ! -f "/opt/c-ares/current/lib/libcares.a" ]; then
  echo "Compiling c-ares..."
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
else
  echo "c-ares: OK"
fi

BOOST_VERSION=v1.90.0
BOOST_STRING=boost_1_90_0
cd /opt/boost
if [ ! -f "/opt/boost/boost_1_90_0.tar" ]; then
  wget -c "https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.bz2"
  echo "Extracting boost $BOOST_VERSION ..."
  bzip2 -d /opt/boost/boost_1_90_0.tar.bz2
fi
mkdir -p /opt/boost/current
mkdir -vp /opt/boost/${BOOST_VERSION}

if [ ! -f "/opt/boost/$BOOST_VERSION/$BOOST_STRING/bootstrap.sh" ]; then
  echo "Extracting boost archive..."
  tar xf /opt/boost/${BOOST_STRING}.tar -C /opt/boost/${BOOST_VERSION}
fi
cd /opt/boost/${BOOST_VERSION}/${BOOST_STRING}/

if [ ! -f "/opt/boost/$BOOST_VERSION/$BOOST_STRING/project-config.jam" ]; then
  echo "Processing boost $BOOST_VERSION bootstrap..."
  ./bootstrap.sh --prefix=/opt/boost/current --with-python=python3
fi

# TODO: Improve complete compiled library detection
if [ ! -f "/opt/boost/current/lib/libboost_atomic.so" ]; then
  echo "Compiling boost..."
  /opt/boost/${BOOST_VERSION}/${BOOST_STRING}/b2 -j$(nproc) threading=multi variant=release install
fi

# TODO: ln -sv /opt/yamlcpp/current/lib/pkgconfig/yaml-cpp.pc /usr/lib/pkgconfig/yaml-cpp.pc
YAML_CPP_VERSION=v0.9.0
YAML_CPP_STRING=yaml-cpp-0.9.0
if [ ! -f "/opt/yamlcpp/current/lib/libyaml-cpp.a" ]; then
  mkdir -p /opt/yamlcpp/current
  rm -rf /opt/yamlcpp/current/*
  rm -rf /opt/yamlcpp/${YAML_CPP_VERSION}
  git clone -b ${YAML_CPP_STRING} https://github.com/jbeder/yaml-cpp.git /opt/yamlcpp/${YAML_CPP_VERSION}
  rm -rf /opt/yamlcpp/${YAML_CPP_VERSION}/.git
  mkdir -p /opt/yamlcpp/${YAML_CPP_VERSION}/build
  cd /opt/yamlcpp/${YAML_CPP_VERSION}/build
  cmake .. -DCMAKE_INSTALL_PREFIX=/opt/yamlcpp/current -DCMAKE_BUILD_TYPE=Release
  make -j$(nproc)
  make install
fi

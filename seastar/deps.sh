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

# TODO: /usr/lib/pkgconfig/boost.pc
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
export BOOST_ROOT=/opt/boost/current
export CMAKE_PREFIX_PATH="/opt/boost/current:${CMAKE_PREFIX_PATH}"

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

# TODO: ln -sv /opt/fmt/current/lib/pkgconfig/fmt.pc /usr/lib/pkgconfig/fmt.pc
FMT_VERSION=v12.1.0
FMT_TAG=12.1.0
if [ ! -f "/opt/fmt/current/lib/libfmt.a" ]; then
  echo "Compiling fmt ${FMT_VERSION} ..."
  mkdir -p /opt/fmt/current
  rm -rf /opt/fmt/current/*
  rm -rf /opt/fmt/${FMT_VERSION}
  git clone -b ${FMT_TAG} https://github.com/fmtlib/fmt.git /opt/fmt/${FMT_VERSION}
  rm -rf /opt/fmt/${CARES_VERSION}/.git
  rm -rf /opt/fmt/${CARES_VERSION}/.github
  mkdir -p /opt/fmt/${FMT_VERSION}/build
  cd /opt/fmt/${FMT_VERSION}/build
  cmake .. -DCMAKE_INSTALL_PREFIX=/opt/fmt/current -DCMAKE_BUILD_TYPE=Release
  make -j$(nproc)
  make install
fi
export CMAKE_PREFIX_PATH="/opt/fmt/current:${CMAKE_PREFIX_PATH}"

# TODO: ln -sv /opt/hwloc/current/lib/pkgconfig/hwloc.pc /usr/lib/pkgconfig/hwloc.pc
HWLOC_VERSION=v2.13.0
HWLOC_TAG=hwloc-2.13.0
if [ ! -f "/opt/hwloc/current/lib/libhwloc.la" ]; then
  echo "Compiling hwloc ${HWLOC_VERSION} ..."
  mkdir -p /opt/hwloc/current
  rm -rf /opt/hwloc/current/*
  rm -rf /opt/hwloc/${HWLOC_VERSION}
  git clone -b ${HWLOC_TAG} https://github.com/open-mpi/hwloc.git /opt/hwloc/${HWLOC_VERSION}
  rm -rf /opt/hwloc/${HWLOC_VERSION}/.git
  rm -rf /opt/hwloc/${HWLOC_VERSION}/.github
  cd /opt/hwloc/${HWLOC_VERSION}
  ./autogen.sh
  ./configure --prefix=/opt/hwloc/current
  make -j$(nproc)
  make install
fi

# TODO: ln -sv /opt/protobuf/current/lib/pkgconfig/protobuf.pc /usr/lib/pkgconfig/protobuf.pc
PROTOBUF_VERSION=v25.9
if [ ! -f "/opt/protobuf/current/lib/libprotobuf.a" ]; then
  echo "Compiling protobuf ${PROTOBUF_VERSION} ..."
  mkdir -p /opt/protobuf/current
  rm -rf /opt/protobuf/current/*
  if [ ! -f "/opt/protobuf/$PROTOBUF_VERSION/CMakeLists.txt" ]; then
    rm -rf /opt/protobuf/${PROTOBUF_VERSION}
    git clone -b ${PROTOBUF_VERSION} https://github.com/protocolbuffers/protobuf.git /opt/protobuf/${PROTOBUF_VERSION}
    cd /opt/protobuf/${PROTOBUF_VERSION}
    git submodule update --init --recursive
    rm -rf /opt/protobuf/${PROTOBUF_VERSION}/.git
    rm -rf /opt/protobuf/${PROTOBUF_VERSION}/.github
  fi
  mkdir -p /opt/protobuf/${PROTOBUF_VERSION}/build
  cd /opt/protobuf/${PROTOBUF_VERSION}/build
  cmake .. -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/opt/protobuf/current -DCMAKE_BUILD_TYPE=Release
  make -j$(nproc)
  make install
fi
export CMAKE_PREFIX_PATH="/opt/protobuf/current:${CMAKE_PREFIX_PATH}"

RAGEL_VERSION=v7.0.4
COLM_SUITE_VERSION=main
COLM_SUITE_TAG=8841e56
if [ ! -f "/opt/colm-suite/current/lib/libragel.a" ]; then
  echo "Compiling colm suite ..."
  mkdir -p /opt/colm-suite/current
  rm -rf /opt/colm-suite/current/*
  rm -rf /opt/colm-suite/${COLM_SUITE_TAG}
  git clone -b main https://github.com/adrian-thurston/colm-suite.git /opt/colm-suite/${COLM_SUITE_TAG}
  cd /opt/colm-suite/${COLM_SUITE_TAG}
  git reset --hard ${COLM_SUITE_TAG}
  rm -rf /opt/colm-suite/${COLM_SUITE_TAG}/.git
  rm -rf /opt/colm-suite/${COLM_SUITE_TAG}/.github
  ./autogen.sh
  ./configure --prefix=/opt/colm-suite/current
  make -j$(nproc)
  make install
fi
export CMAKE_PREFIX_PATH="/opt/colm-suite/current:${CMAKE_PREFIX_PATH}"

SEASTAR_VERSION=v25.05.0
SEASTAR_TAG=seastar-25.05.0
mkdir -p /opt/seastar/current
rm -rf /opt/seastar/current/*
rm -rf /opt/seastar/${SEASTAR_VERSION}
git clone -b ${SEASTAR_TAG} https://github.com/scylladb/seastar.git /opt/seastar/${SEASTAR_VERSION}
rm -rf /opt/seastar/${SEASTAR_VERSION}/.git
cd /opt/seastar/${SEASTAR_VERSION}/
./configure.py --mode=release --without-tests --without-apps --without-demos --prefix=/opt/seastar/current

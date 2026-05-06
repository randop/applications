#!/bin/sh

# exit on error, error on unset variables
set -eu

# packages:
# pacman -S xfsprogs

OPT_PREFIX=$HOME/opt

if [ ! -d "${OPT_PREFIX}" ]; then
  mkdir -p ${OPT_PREFIX}
else
  echo "${OPT_PREFIX} directory: OK"
fi

LOCAL_PKGCONFIG=${HOME}/.local/lib/pkgconfig
if [ ! -d "${LOCAL_PKGCONFIG}" ]; then
  mkdir -p ${LOCAL_PKGCONFIG}
fi

LOCAL_BIN=${HOME}/.local/bin
if [ ! -d "${LOCAL_BIN}" ]; then
  mkdir -p ${LOCAL_BIN}
fi

if [ -n "${PKG_CONFIG_PATH+set}" ]; then
  export PKG_CONFIG_PATH="${LOCAL_PKGCONFIG}:${PKG_CONFIG_PATH}"
else
  export PKG_CONFIG_PATH=$LOCAL_PKGCONFIG
fi

# TODO: $LOCAL_PKGCONFIG/boost.pc
BOOST_VERSION=v1.91.0
BOOST_STRING=boost-1.91.0-1
mkdir -p ${OPT_PREFIX}/boost/current
cd ${OPT_PREFIX}/boost
if [ ! -f "${OPT_PREFIX}/boost/current/lib/libboost_atomic.so" ]; then
  echo "Downloading boost $BOOST_VERSION ..."
  wget -c "https://github.com/boostorg/boost/releases/download/boost-1.91.0-1/boost-1.91.0-1-b2-nodocs.tar.gz"
  mkdir -p ${OPT_PREFIX}/boost/current
  mkdir -vp ${OPT_PREFIX}/boost/${BOOST_VERSION}
  tar xzf ${OPT_PREFIX}/boost/boost-1.91.0-1-b2-nodocs.tar.gz -C ${OPT_PREFIX}/boost/${BOOST_VERSION}
  cd ${OPT_PREFIX}/boost/${BOOST_VERSION}/${BOOST_STRING}/

  if [ ! -f "${OPT_PREFIX}/boost/$BOOST_VERSION/$BOOST_STRING/project-config.jam" ]; then
    echo "Processing boost $BOOST_VERSION bootstrap..."
    ./bootstrap.sh --prefix=${OPT_PREFIX}/boost/current
  fi
fi
export BOOST_ROOT=${OPT_PREFIX}/boost/current

if [ -n "${CMAKE_PREFIX_PATH+set}" ]; then
  CMAKE_PREFIX_PATH="${OPT_PREFIX}/boost/current:${CMAKE_PREFIX_PATH}"
else
  CMAKE_PREFIX_PATH="${OPT_PREFIX}/boost/current"
fi
export CMAKE_PREFIX_PATH

# TODO: Improve complete compiled library detection
if [ ! -f "${OPT_PREFIX}/boost/current/lib/libboost_atomic.so" ]; then
  echo "Compiling boost..."
  ${OPT_PREFIX}/boost/${BOOST_VERSION}/${BOOST_STRING}/b2 -j$(nproc) threading=multi variant=release install
else
  echo "boost: OK"
fi

CARES_VERSION=v1.34.6
if [ ! -f "${OPT_PREFIX}/c-ares/current/lib/libcares.a" ]; then
  echo "Compiling c-ares..."
  mkdir -p ${OPT_PREFIX}/c-ares/current
  rm -rf ${OPT_PREFIX}/c-ares/${CARES_VERSION}
  git clone -b ${CARES_VERSION} https://github.com/c-ares/c-ares.git ${OPT_PREFIX}/c-ares/${CARES_VERSION}
  rm -rf ${OPT_PREFIX}/c-ares/${CARES_VERSION}/.git
  rm -rf ${OPT_PREFIX}/c-ares/${CARES_VERSION}/.github
  cd ${OPT_PREFIX}/c-ares/${CARES_VERSION}
  autoreconf -fi
  ./configure --prefix=${OPT_PREFIX}/c-ares/current
  make -j$(nproc)
  make install
  echo "Checking compiled c-ares library..." && file ${OPT_PREFIX}/c-ares/current/lib/libcares.a
  rm -f ${LOCAL_PKGCONFIG}/libcares.pc
else
  echo "c-ares: OK"
fi

if [ ! -f "${LOCAL_PKGCONFIG}/libcares.pc" ]; then
  ln -sv ${OPT_PREFIX}/c-ares/current/lib/pkgconfig/libcares.pc ${LOCAL_PKGCONFIG}/libcares.pc
fi
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/c-ares/current:${CMAKE_PREFIX_PATH}"

YAML_CPP_VERSION=v0.9.0
YAML_CPP_STRING=yaml-cpp-0.9.0
if [ ! -f "${OPT_PREFIX}/yamlcpp/current/lib/libyaml-cpp.a" ]; then
  mkdir -p ${OPT_PREFIX}/yamlcpp/current
  rm -rf ${OPT_PREFIX}/yamlcpp/current/*
  rm -rf ${OPT_PREFIX}/yamlcpp/${YAML_CPP_VERSION}
  git clone -b ${YAML_CPP_STRING} https://github.com/jbeder/yaml-cpp.git ${OPT_PREFIX}/yamlcpp/${YAML_CPP_VERSION}
  rm -rf ${OPT_PREFIX}/yamlcpp/${YAML_CPP_VERSION}/.git
  mkdir -p ${OPT_PREFIX}/yamlcpp/${YAML_CPP_VERSION}/build
  cd ${OPT_PREFIX}/yamlcpp/${YAML_CPP_VERSION}/build
  cmake .. -DCMAKE_INSTALL_PREFIX=${OPT_PREFIX}/yamlcpp/current -DCMAKE_BUILD_TYPE=Release
  make -j$(nproc)
  make install
  rm -f $LOCAL_PKGCONFIG/yaml-cpp.pc
else
  echo "yaml-cpp: OK"
fi

if [ ! -f "${LOCAL_PKGCONFIG}/yaml-cpp.pc" ]; then
  ln -sv ${OPT_PREFIX}/yamlcpp/current/lib/pkgconfig/yaml-cpp.pc ${LOCAL_PKGCONFIG}/yaml-cpp.pc
fi
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/yamlcpp/current:${CMAKE_PREFIX_PATH}"

FMT_VERSION=v11.2.0
FMT_TAG=11.2.0
if [ ! -f "${OPT_PREFIX}/fmt/current/lib/libfmt.a" ]; then
  echo "Compiling fmt ${FMT_VERSION} ..."
  mkdir -p ${OPT_PREFIX}/fmt/current
  rm -rf ${OPT_PREFIX}/fmt/current/*
  rm -rf ${OPT_PREFIX}/fmt/${FMT_VERSION}
  git clone -b ${FMT_TAG} https://github.com/fmtlib/fmt.git ${OPT_PREFIX}/fmt/${FMT_VERSION}
  rm -rf ${OPT_PREFIX}/fmt/${CARES_VERSION}/.git
  rm -rf ${OPT_PREFIX}/fmt/${CARES_VERSION}/.github
  mkdir -p ${OPT_PREFIX}/fmt/${FMT_VERSION}/build
  cd ${OPT_PREFIX}/fmt/${FMT_VERSION}/build
  cmake .. -DCMAKE_INSTALL_PREFIX=${OPT_PREFIX}/fmt/current -DCMAKE_BUILD_TYPE=Release
  make -j$(nproc)
  make install
  rm -f ${LOCAL_PKGCONFIG}/fmt.pc
else
  echo "fmt: OK"
fi

if [ ! -f "${LOCAL_PKGCONFIG}/fmt.pc" ]; then
  ln -sv ${OPT_PREFIX}/fmt/current/lib/pkgconfig/fmt.pc ${LOCAL_PKGCONFIG}/fmt.pc
fi
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/fmt/current:${CMAKE_PREFIX_PATH}"

HWLOC_VERSION=v2.13.0
HWLOC_TAG=hwloc-2.13.0
if [ ! -f "${OPT_PREFIX}/hwloc/current/lib/libhwloc.la" ]; then
  echo "Compiling hwloc ${HWLOC_VERSION} ..."
  mkdir -p ${OPT_PREFIX}/hwloc/current
  rm -rf ${OPT_PREFIX}/hwloc/current/*
  rm -rf ${OPT_PREFIX}/hwloc/${HWLOC_VERSION}
  git clone -b ${HWLOC_TAG} https://github.com/open-mpi/hwloc.git ${OPT_PREFIX}/hwloc/${HWLOC_VERSION}
  rm -rf ${OPT_PREFIX}/hwloc/${HWLOC_VERSION}/.git
  rm -rf ${OPT_PREFIX}/hwloc/${HWLOC_VERSION}/.github
  cd ${OPT_PREFIX}/hwloc/${HWLOC_VERSION}
  ./autogen.sh
  ./configure --prefix=${OPT_PREFIX}/hwloc/current
  make -j$(nproc)
  make install
else
  echo "hwloc: OK"
fi
if [ ! -f "${LOCAL_PKGCONFIG}/hwloc.pc" ]; then
  ln -sv ${OPT_PREFIX}/hwloc/current/lib/pkgconfig/hwloc.pc ${LOCAL_PKGCONFIG}/hwloc.pc
fi
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/hwloc/current:${CMAKE_PREFIX_PATH}"

PROTOBUF_VERSION=v25.9
if [ ! -f "${OPT_PREFIX}/protobuf/current/lib/libprotobuf.a" ]; then
  echo "Compiling protobuf ${PROTOBUF_VERSION} ..."
  mkdir -p ${OPT_PREFIX}/protobuf/current
  rm -rf ${OPT_PREFIX}/protobuf/current/*
  if [ ! -f "${OPT_PREFIX}/protobuf/$PROTOBUF_VERSION/CMakeLists.txt" ]; then
    rm -rf ${OPT_PREFIX}/protobuf/${PROTOBUF_VERSION}
    git clone -b ${PROTOBUF_VERSION} https://github.com/protocolbuffers/protobuf.git ${OPT_PREFIX}/protobuf/${PROTOBUF_VERSION}
    cd ${OPT_PREFIX}/protobuf/${PROTOBUF_VERSION}
    git submodule update --init --recursive
    rm -rf ${OPT_PREFIX}/protobuf/${PROTOBUF_VERSION}/.git
    rm -rf ${OPT_PREFIX}/protobuf/${PROTOBUF_VERSION}/.github
  fi
  mkdir -p ${OPT_PREFIX}/protobuf/${PROTOBUF_VERSION}/build
  cd ${OPT_PREFIX}/protobuf/${PROTOBUF_VERSION}/build
  cmake .. -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=${OPT_PREFIX}/protobuf/current -DCMAKE_BUILD_TYPE=Release
  make -j$(nproc)
  make install
  rm -f ${LOCAL_PKGCONFIG}/profobuf.pc
else
  echo "protobuf: OK"
fi
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/protobuf/current:${CMAKE_PREFIX_PATH}"
if [ ! -f "${LOCAL_PKGCONFIG}/protobuf.pc" ]; then
  ln -sv ${OPT_PREFIX}/protobuf/current/lib/pkgconfig/protobuf.pc ${LOCAL_PKGCONFIG}/protobuf.pc
fi

LKSCTP_TOOLS_VERSION=v1.0.21
if [ ! -f "${OPT_PREFIX}/lksctp/current/lib/libsctp.a" ]; then
  echo "Compiling lksctp-tools ${LKSCTP_TOOLS_VERSION} ..."
  mkdir -p ${OPT_PREFIX}/lksctp/current
  rm -rf ${OPT_PREFIX}/lksctp/current/*
  rm -rf ${OPT_PREFIX}/lksctp/${LKSCTP_TOOLS_VERSION}
  git clone -b ${LKSCTP_TOOLS_VERSION} https://github.com/sctp/lksctp-tools.git ${OPT_PREFIX}/lksctp/${LKSCTP_TOOLS_VERSION}
  cd ${OPT_PREFIX}/lksctp/${LKSCTP_TOOLS_VERSION}
  chmod +x ./bootstrap
  ./bootstrap
  chmod +x ./configure
  ./configure --prefix=${OPT_PREFIX}/lksctp/current --disable-tests
  make -j$(nproc)
  make install
  rm -f ${LOCAL_PKGCONFIG}/libsctp.pc
else
  echo "lksctp: OK"
fi
if [ ! -f "${LOCAL_PKGCONFIG}/libsctp.pc" ]; then
  ln -sv ${OPT_PREFIX}/lksctp/current/lib/pkgconfig/libsctp.pc ${LOCAL_PKGCONFIG}/libsctp.pc
fi
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/lksctp/current:${CMAKE_PREFIX_PATH}"

RAGEL_VERSION=v7.0.4
COLM_SUITE_VERSION=main
COLM_SUITE_TAG=8841e56
if [ ! -f "${OPT_PREFIX}/colm-suite/current/lib/libragel.a" ]; then
  echo "Compiling colm suite ..."
  mkdir -p ${OPT_PREFIX}/colm-suite/current
  rm -rf ${OPT_PREFIX}/colm-suite/current/*
  rm -rf ${OPT_PREFIX}/colm-suite/${COLM_SUITE_TAG}
  git clone -b main https://github.com/adrian-thurston/colm-suite.git ${OPT_PREFIX}/colm-suite/${COLM_SUITE_TAG}
  cd ${OPT_PREFIX}/colm-suite/${COLM_SUITE_TAG}
  git reset --hard ${COLM_SUITE_TAG}
  rm -rf ${OPT_PREFIX}/colm-suite/${COLM_SUITE_TAG}/.git
  rm -rf ${OPT_PREFIX}/colm-suite/${COLM_SUITE_TAG}/.github
  ./autogen.sh
  ./configure --prefix=${OPT_PREFIX}/colm-suite/current
  make -j$(nproc)
  make install
else
  echo "ragel: OK"
fi
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/colm-suite/current:${CMAKE_PREFIX_PATH}"

VALGRIND_VERSION=3.26.0
mkdir -p ${OPT_PREFIX}/valgrind/current
cd ${OPT_PREFIX}/valgrind
mkdir -p ${OPT_PREFIX}/valgrind/${VALGRIND_VERSION}
if [ ! -f "${OPT_PREFIX}/valgrind/valgrind-${VALGRIND_VERSION}.tar" ]; then
  wget -c "https://sourceware.org/pub/valgrind/valgrind-${VALGRIND_VERSION}.tar.bz2"
  echo "Extracting valgrind ..."
  bzip2 -d ${OPT_PREFIX}/valgrind/valgrind-${VALGRIND_VERSION}.tar.bz2
fi
if [ ! -f "${OPT_PREFIX}/valgrind/${VALGRIND_VERSION}/valgrind-${VALGRIND_VERSION}/autogen.sh" ]; then
  echo "Extracting valgrind archive ..."
  tar xf ${OPT_PREFIX}/valgrind/valgrind-${VALGRIND_VERSION}.tar -C ${OPT_PREFIX}/valgrind/${VALGRIND_VERSION}
fi

# TODO: Ehnance valgrid library detection
if [ ! -d "${OPT_PREFIX}/valgrind/current/lib/valgrind" ]; then
  echo "Compiling valgrind ${VALGRIND_VERSION} ..."
  cd ${OPT_PREFIX}/valgrind/${VALGRIND_VERSION}/valgrind-${VALGRIND_VERSION}
  ./autogen.sh
  ./configure --prefix=${OPT_PREFIX}/valgrind/current
  make -j$(nproc)
  make install
  rm -f ${LOCAL_PKGCONFIG}/valgrind.pc
else
  echo "valgrind: OK"
fi

if [ ! -f "${LOCAL_PKGCONFIG}/valgrind.pc" ]; then
  ln -sv ${OPT_PREFIX}/valgrind/current/lib/pkgconfig/valgrind.pc ${LOCAL_PKGCONFIG}/valgrind.pc
fi
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/valgrind/current:${CMAKE_PREFIX_PATH}"

DOXYGEN_VERSION=v1.61.1
DOXYGEN_TAG=Release_1_16_1
if [ -f "$HOME/opt/doxygen/current/bin/doxygen" ]; then
  cp -v ${OPT_PREFIX}/doxygen/current/bin/doxygen $HOME/.local/bin/doxygen
fi
if [ ! -f "$HOME/.local/bin/doxygen" ]; then
  mkdir -p ${OPT_PREFIX}/doxygen/current
  rm -rf ${OPT_PREFIX}/doxygen/${DOXYGEN_VERSION}
  git clone -b ${DOXYGEN_TAG} https://github.com/doxygen/doxygen.git ${OPT_PREFIX}/doxygen/${DOXYGEN_VERSION}
  rm -rf ${OPT_PREFIX}/doxygen/${DOXYGEN_VERSION}/.git
  mkdir -p ${OPT_PREFIX}/doxygen/${DOXYGEN_VERSION}/build
  cd ${OPT_PREFIX}/doxygen/${DOXYGEN_VERSION}/build
  cmake .. -DCMAKE_INSTALL_PREFIX=${OPT_PREFIX}/doxygen/current -DCMAKE_BUILD_TYPE=Release
  make -j$(nproc)
  make install
  cp -v ${OPT_PREFIX}/doxygen/current/bin/doxygen $HOME/.local/bin/doxygen
else
  echo "doxygen: OK"
fi
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/doxygen/current:${CMAKE_PREFIX_PATH}"

LIBURING_VERSION=2.14
LIBURING_TAG=liburing-2.14
if [ ! -f "${OPT_PREFIX}/liburing/current/lib/liburing.a" ]; then
  echo "Compiling liburing ${LIBURING_VERSION}"
  mkdir -p ${OPT_PREFIX}/liburing/current
  rm -rf ${OPT_PREFIX}/liburing/current/*
  rm -rf ${OPT_PREFIX}/liburing/${LIBURING_VERSION}
  git clone -b ${LIBURING_TAG} https://github.com/axboe/liburing.git ${OPT_PREFIX}/liburing/${LIBURING_VERSION}
  rm -rf ${OPT_PREFIX}/liburing/${LIBURING_VERSION}/.git
  rm -rf ${OPT_PREFIX}/liburing/${LIBURING_VERSION}/.github
  cd ${OPT_PREFIX}/liburing/${LIBURING_VERSION}
  ./configure --prefix=${OPT_PREFIX}/liburing/current
  make -j$(nproc)
  make install
else
  echo "liburing: OK"
fi
if [ ! -f "${LOCAL_PKGCONFIG}/liburing.pc" ]; then
  ln -sv ${OPT_PREFIX}/liburing/current/lib/pkgconfig/liburing.pc ${LOCAL_PKGCONFIG}/liburing.pc
fi
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/liburing/current:${CMAKE_PREFIX_PATH}"

detect_ubuntu_jammy() {
  if [ -f /etc/os-release ]; then
    . /etc/os-release
    [ "$ID" = "ubuntu" ] && [ "$VERSION_ID" = "22.04" ]
    return $?
  fi

  if [ -f /etc/lsb-release ]; then
    . /etc/lsb-release
    [ "$DISTRIB_ID" = "Ubuntu" ] && [ "$DISTRIB_RELEASE" = "22.04" ]
    return $?
  fi

  if [ -f /etc/issue ]; then
    case $(cat /etc/issue) in
    *"Ubuntu 22.04"*) return 0 ;;
    esac
    return 1
  fi

  return 2
}

SEASTAR_VERSION=v25.05.0
SEASTAR_TAG=seastar-25.05.0
SEASTAR_PKG_VERSION=
if [ -f "${HOME}/.local/lib/pkgconfig/seastar.pc" ]; then
  SEASTAR_PKG_VERSION=$(pkg-config --modversion seastar 2>/dev/null)
fi

echo "PKG_CONFIG_PATH: $PKG_CONFIG_PATH"
echo "CMAKE_PREFIX_PATH: $CMAKE_PREFIX_PATH"

if [ -z "$SEASTAR_PKG_VERSION" ]; then
  echo "Compiling seastar ${SEASTAR_VERSION} ..."
  mkdir -p ${OPT_PREFIX}/seastar/current
  rm -rf ${OPT_PREFIX}/seastar/current/*
  rm -rf ${OPT_PREFIX}/seastar/${SEASTAR_VERSION}
  git clone -b ${SEASTAR_TAG} https://github.com/scylladb/seastar.git ${OPT_PREFIX}/seastar/${SEASTAR_VERSION}
  rm -rf ${OPT_PREFIX}/seastar/${SEASTAR_VERSION}/.git
  cd ${OPT_PREFIX}/seastar/${SEASTAR_VERSION}
  if detect_ubuntu_jammy; then
    echo "Ubuntu 22.04 LTS (Jammy Jellyfish) detected."
    export CC=gcc-13
    export CXX=g++-13

    ./configure.py \
      --mode=release \
      --without-tests \
      --without-apps \
      --without-demos \
      --enable-io_uring \
      --cflags="-I${OPT_PREFIX}/hwloc/current/include" \
      --compiler=g++-13 \
      --prefix=${OPT_PREFIX}/seastar/current
  else
    ./configure.py \
      --mode=release \
      --without-tests \
      --without-apps \
      --without-demos \
      --enable-io_uring \
      --cflags="-I${OPT_PREFIX}/hwloc/current/include" \
      --prefix=${OPT_PREFIX}/seastar/current
  fi
  ninja -C build/release install
  rm -fv ${LOCAL_PKGCONFIG}/seastar.pc
  ln -sv ${OPT_PREFIX}/seastar/current/lib/pkgconfig/seastar.pc ${LOCAL_PKGCONFIG}/seastar.pc
else
  echo "Seastar: $SEASTAR_PKG_VERSION"
fi

FROM debian:stable-slim AS builder

ARG IMAGE_VERSION=1.3.0
ARG BOOST_VERSION=1.92.0
ARG BOOST_TAG=boost_1_92_0

# Install build tools and dependencies
RUN echo "Installing build packages..." && \
    apt update && apt install -y --no-install-recommends \
    build-essential \
    meson \
    cmake \
    autotools-dev \
    libicu-dev \
    libbz2-dev \
    wget \
    ca-certificates \
    pkg-config \
    libz-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

ADD https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.gz /opt/boost_1_92_0.tar.gz

RUN echo "Extracting Boost..." && \
    tar xzf /opt/${BOOST_TAG}.tar.gz -C /opt

RUN echo "Compiling and installing Boost ${BOOST_VERSION}..." && \
    cd /opt/${BOOST_TAG} && /opt/${BOOST_TAG}/bootstrap.sh \
    && /opt/${BOOST_TAG}/b2 --prefix=/usr/local \
      link=static \
      variant=release \
      threading=multi \
      runtime-link=shared \
      -j$(nproc) install \
    && rm -rf /opt/${BOOST_TAG}

RUN echo "Cleaning up..." && \
    rm /opt/${BOOST_TAG}.tar.gz

RUN echo "Linking Boost..." && \
    echo "/usr/local/lib" > /etc/ld.so.conf.d/boost.conf \
    && ldconfig

FROM debian:stable-slim AS builder

ARG IMAGE_VERSION=1.3.1
ARG BOOST_VERSION=1.92.0
ARG BOOST_TAG=boost_1_92_0

RUN apt update && apt install -y --no-install-recommends \
        build-essential meson cmake autotools-dev \
        libicu-dev libbz2-dev libz-dev libssl-dev \
        wget ca-certificates pkg-config \
    && rm -rf /var/lib/apt/lists/* \
    && wget -q https://archives.boost.io/release/${BOOST_VERSION}/source/${BOOST_TAG}.tar.gz -O /tmp/boost.tar.gz \
    && tar xzf /tmp/boost.tar.gz -C /tmp \
    && cd /tmp/${BOOST_TAG} \
    && ./bootstrap.sh \
    && ./b2 --prefix=/usr/local \
          link=static \
          variant=release \
          threading=multi \
          runtime-link=shared \
          -j$(nproc) install \
    && rm -rf /tmp/${BOOST_TAG} /tmp/boost.tar.gz \
    && echo "/usr/local/lib" > /etc/ld.so.conf.d/boost.conf \
    && ldconfig

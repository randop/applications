FROM debian:bookworm-slim AS builder

ARG IMAGE_VERSION=1.0.0
ARG BOOST_VERSION=1.88.0

# Install build tools and dependencies
RUN echo "Installing build packages..." && \
    apt update && apt install -y --no-install-recommends \
    build-essential \
    meson \
    cmake \
    libpq-dev \
    g++ \
    python3-dev \
    autotools-dev \
    libicu-dev \
    libbz2-dev \
    wget \
    ca-certificates \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

ADD https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.gz /opt/boost_1_88_0.tar.gz

RUN echo "Extracting Boost..." && \
    tar xzf /opt/boost_1_88_0.tar.gz -C /opt

RUN echo "Compiling and installing Boost..." && \
    cd /opt/boost_1_88_0 && /opt/boost_1_88_0/bootstrap.sh --prefix=/usr/local --with-python=python3 \
    && /opt/boost_1_88_0/b2 threading=multi link=static install \
    && rm -rf /opt/boost_1_88_0

RUN echo "Cleaning up..." && \
    rm /opt/boost_1_88_0.tar.gz

RUN echo "Linking Boost..." && \
    echo "/usr/local/lib" > /etc/ld.so.conf.d/boost.conf \
    && ldconfig
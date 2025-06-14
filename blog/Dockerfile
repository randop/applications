# Build stage: Use Debian Bookworm Slim to compile C++ project
FROM --platform=$BUILDPLATFORM debian:bookworm-slim AS builder

# Install build tools and dependencies
RUN apt update && apt install -y --no-install-recommends \
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

RUN wget --continue https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.gz -O /tmp/boost_1_88_0.tar.gz

RUN tar xzf /tmp/boost_1_88_0.tar.gz -C /tmp

RUN cd /tmp/boost_1_88_0 && /tmp/boost_1_88_0/bootstrap.sh --prefix=/usr/local --with-python=python3 \
    && /tmp/boost_1_88_0/b2 threading=multi link=static install \
    && rm -rf /tmp/boost_1_88_0

RUN rm /tmp/boost_1_88_0.tar.gz

RUN echo "/usr/local/lib" > /etc/ld.so.conf.d/boost.conf \
    && ldconfig

WORKDIR /app
COPY . .

ENV LD_LIBRARY_PATH=/usr/local/lib

RUN meson setup build --prefer-static --default-library=static
RUN meson compile -C build

# Runtime stage: Use Debian Bookworm Slim for runtime
FROM debian:bookworm-slim

# Install runtime dependencies for s6-overlay and healthcheck
RUN apt update && apt install -y --no-install-recommends \
    libpq5 \
    openssl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create nonroot user and group
RUN groupadd -r nonroot && useradd -r -g nonroot -d /home/nonroot -s /sbin/nologin nonroot && \
    mkdir -p /home/nonroot && chown nonroot:nonroot /home/nonroot

# Copy compiled C++ service and healthcheck script from builder
WORKDIR /app
COPY --from=builder --chown=nonroot:nonroot /app/build/blog /app/

# Set ownership for app directory
RUN chown -R nonroot:nonroot /app

# Switch to nonroot user
USER nonroot

EXPOSE 10000

CMD ["/app/blog"]
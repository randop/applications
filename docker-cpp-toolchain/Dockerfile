# Build stage: Use pre-built Boost C++ image
FROM rfledesma/boost:latest AS builder

WORKDIR /app
COPY . .

# Install development dependencies
RUN echo "Installing development packages..." && \
    apt update && apt install -y --no-install-recommends \
    git \
    && rm -rf /var/lib/apt/lists/*

RUN echo "Compiling mongodb c driver.." && \
    mkdir -p /opt/mongo-c-driver/current && \
    git clone -b 2.0.2 --depth 1 https://github.com/mongodb/mongo-c-driver.git /opt/mongo-c-driver/2.0.2 && \
    cd /opt/mongo-c-driver/2.0.2 && cmake -DCMAKE_INSTALL_PREFIX=/opt/mongo-c-driver/current . && \
    cd /opt/mongo-c-driver/2.0.2 && make all install && \
    echo "/opt/mongo-c-driver/current" > /etc/ld.so.conf.d/boost.conf && \
    ldconfig

ENV LD_LIBRARY_PATH=/usr/local/lib:/opt/mongo-c-driver/current/lib

RUN echo "Linking MongoDB C Driver..." && \
    echo "/opt/mongo-c-driver/current/lib" > /etc/ld.so.conf.d/mongoc.conf \
    && ldconfig
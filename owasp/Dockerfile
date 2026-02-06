ARG RELEASE_VERSION="1.1.6"

ARG NGINX_VERSION=1.29.5
ARG GEO_DB_RELEASE=2026-02
ARG MODSEC_TAG=v3.0.14
ARG CORERULESET_TAG=v4.23.0

# Nginx base
FROM archlinux:base AS base

ARG NGINX_VERSION
ARG GEO_DB_RELEASE
ARG MODSEC_TAG
ARG CORERULESET_TAG

# Initialize pacman keyring and update system
RUN echo "Updating nginx base..." && \
    pacman-key --init && \
    pacman-key --populate archlinux && \
    pacman -Syu --noconfirm && \
    pacman -S --noconfirm --needed \
    nginx-mainline \
    nginx-mainline-src

# Build stage
FROM archlinux:base AS builder

ARG NGINX_VERSION
ARG GEO_DB_RELEASE
ARG MODSEC_TAG
ARG CORERULESET_TAG

# Download IP to Geolocation databases
ADD https://download.db-ip.com/free/dbip-country-lite-2026-02.mmdb.gz /tmp/dbip-country-lite.mmdb.gz
ADD https://download.db-ip.com/free/dbip-city-lite-2026-02.mmdb.gz /tmp/dbip-city-lite.mmdb.gz

RUN echo "Extracting IP to Geolocation databases..." && \
  mkdir -p /etc/nginx/geoip && \
  gunzip -c /tmp/dbip-city-lite.mmdb.gz > /etc/nginx/geoip/dbip-city-lite.mmdb && \
  gunzip -c /tmp/dbip-country-lite.mmdb.gz > /etc/nginx/geoip/dbip-country-lite.mmdb && \
  rm -f /tmp/dbip*

WORKDIR /opt

# Initialize pacman keyring and update system
RUN echo "Updating build operating system..." && \
    pacman-key --init && \
    pacman-key --populate archlinux && \
    pacman -Syu --noconfirm

# Install build dependencies
RUN echo "Installing build components and dependencies..." && \
    pacman -S --noconfirm --needed \
    base-devel \
    git \
    wget \
    curl \
    pcre \
    pcre2 \
    libxml2 \
    curl \
    openssl \
    zlib \
    libmaxminddb \
    lmdb \
    yajl \
    geoip \
    libtool \
    autoconf \
    automake

# Clone and compile modsecurity. Binary will be located in /usr/local/modsecurity
RUN echo "Building ModSec Library..." && \
    git clone --depth 1 --branch $MODSEC_TAG https://github.com/SpiderLabs/ModSecurity.git && \
    git -C /opt/ModSecurity submodule update --init --recursive && \
    (cd "/opt/ModSecurity" && \
    ./build.sh && \
    ./configure --with-lmdb && \
    make && \
    make install \
    ) && \
    rm -fr /opt/ModSecurity \
    /usr/local/modsecurity/lib/libmodsecurity.a \
    /usr/local/modsecurity/lib/libmodsecurity.la

# Clone Modsec Nginx Connector, GeoIP, ModSec OWASP Rules, and download/extract nginx and GeoIP databases
RUN echo 'Cloning Modsec Nginx Connector' && \
    git clone -b master --depth 1 https://github.com/SpiderLabs/ModSecurity-nginx.git && \
    wget -O - https://nginx.org/download/nginx-$NGINX_VERSION.tar.gz | tar -xz

RUN echo 'Processing GeoIP...' && \
    git clone -b master --depth 1 https://github.com/leev/ngx_http_geoip2_module.git

RUN echo 'Processing ModSec OWASP Rules...' && \
    git clone -b $CORERULESET_TAG --depth 1 https://github.com/coreruleset/coreruleset.git /usr/local/owasp-modsecurity-crs

# Install GeoIP2 and ModSecurity Nginx modules
RUN echo 'Installing Nginx Modules' && \
    mkdir -p /usr/lib/nginx/modules && \
    (cd "/opt/nginx-$NGINX_VERSION" && \
    ./configure --with-compat \
    --add-dynamic-module=../ModSecurity-nginx \
    --add-dynamic-module=../ngx_http_geoip2_module && \
    make modules \
    ) && \
    cp /opt/nginx-$NGINX_VERSION/objs/ngx_http_modsecurity_module.so \
    /opt/nginx-$NGINX_VERSION/objs/ngx_http_geoip2_module.so \
    /usr/lib/nginx/modules/ && \
    rm -fr /opt/*

# Production stage
FROM archlinux:base AS production

# Install runtime dependencies
RUN echo "Installing runtime components and dependencies..." && \
    pacman-key --init && \
    pacman-key --populate archlinux && \
    pacman -Syu --noconfirm && \
    pacman -S --noconfirm --needed \
    nginx-mainline \
    pcre \
    pcre2 \
    libxml2 \
    curl \
    openssl \
    zlib \
    libmaxminddb \
    lmdb \
    yajl \
    geoip \
    gcc-libs \
    tzdata && \
    pacman -Scc --noconfirm

RUN echo "Setup system accounts..." && \
    groupadd --system --gid 101 nginx && \
    useradd --system --gid nginx --no-create-home --home /nonexistent --comment "nginx user" --shell /bin/false --uid 101 nginx && \
    printf '%s\n' \
    'nginx soft nofile 65535' \
    'nginx hard nofile 65535' \
    >> /etc/security/limits.conf

# Copy nginx, owasp-modsecurity-crs, and modsecurity from the build image
COPY --from=base /etc/nginx/ /etc/nginx/
COPY --from=builder /etc/nginx/geoip/ /etc/nginx/geoip/
COPY --from=builder /usr/local/modsecurity /usr/local/modsecurity
COPY --from=builder /usr/local/owasp-modsecurity-crs /usr/local/owasp-modsecurity-crs
COPY --from=builder /usr/lib/nginx/modules/ /etc/nginx/modules/

# Copy local config files into the image
COPY errors /usr/share/nginx/errors
COPY conf/nginx/ /etc/nginx/
COPY conf/modsec/ /etc/nginx/modsec/
COPY conf/owasp/ /usr/local/owasp-modsecurity-crs/

WORKDIR /usr/share/nginx/html

EXPOSE 80 443

# Start nginx
CMD ["nginx", "-g", "daemon off;"]

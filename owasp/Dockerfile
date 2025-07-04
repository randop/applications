ARG NGINX_VER=1.23.3

FROM nginx:${NGINX_VER}-alpine AS base

ARG GEO_DB_RELEASE=2023-03
ARG MODSEC_TAG=v3.0.8
ARG OWASP_TAG=v3.3.4

WORKDIR /opt

# Install dependencies; includes dependencies required for compile-time options:
# curl, libxml, pcre, and lmdb and Modsec
RUN echo "Installing Dependencies" && \
    apk add --no-cache --virtual general-dependencies \
    autoconf \
    automake \
    byacc \
    curl-dev \
    flex \
    g++ \
    gcc \
    geoip-dev \
    git \
    libc-dev \
    libmaxminddb-dev \
    libstdc++ \
    libtool \
    libxml2-dev \
    linux-headers \
    lmdb-dev \
    make \
    openssl-dev \
    pcre-dev \
    yajl-dev \
    zlib-dev

# Clone and compile modsecurity. Binary will be located in /usr/local/modsecurity
RUN echo "Installing ModSec Library" && \
    git clone -b ${MODSEC_TAG} --depth 1 https://github.com/SpiderLabs/ModSecurity.git && \
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
    git clone -b master --depth 1 https://github.com/leev/ngx_http_geoip2_module.git && \
    mkdir -p /etc/nginx/geoip && \
    wget -O - https://download.db-ip.com/free/dbip-city-lite-2025-06.mmdb.gz | gzip -d > /etc/nginx/geoip/dbip-city-lite.mmdb && \
    wget -O - https://download.db-ip.com/free/dbip-country-lite-2025-06.mmdb.gz | gzip -d > /etc/nginx/geoip/dbip-country-lite.mmdb

RUN echo 'Processing ModSec OWASP Rules...' && \
    git clone -b ${OWASP_TAG} --depth 1 https://github.com/coreruleset/coreruleset.git /usr/local/owasp-modsecurity-crs

# Install GeoIP2 and ModSecurity Nginx modules
RUN echo 'Installing Nginx Modules' && \
    (cd "/opt/nginx-$NGINX_VERSION" && \
    ./configure --with-compat \
    --add-dynamic-module=../ModSecurity-nginx \
    --add-dynamic-module=../ngx_http_geoip2_module && \
    make modules \
    ) && \
    cp /opt/nginx-$NGINX_VERSION/objs/ngx_http_modsecurity_module.so \
    /opt/nginx-$NGINX_VERSION/objs/ngx_http_geoip2_module.so \
    /usr/lib/nginx/modules/ && \
    rm -fr /opt/* && \
    apk del general-dependencies


FROM nginx:${NGINX_VER}-alpine AS production

# Install production dependencies
RUN apk add --no-cache \
    curl-dev \
    libmaxminddb-dev \
    libstdc++ \
    libxml2-dev \
    lmdb-dev \
    pcre \
    tzdata \
    yajl && \
    chown -R nginx:nginx /usr/share/nginx

# Copy nginx, owasp-modsecurity-crs, and modsecurity from the build image
COPY --from=base /etc/nginx/ /etc/nginx/
COPY --from=base /usr/local/modsecurity /usr/local/modsecurity
COPY --from=base /usr/local/owasp-modsecurity-crs /usr/local/owasp-modsecurity-crs
COPY --from=base /usr/lib/nginx/modules/ /usr/lib/nginx/modules/

# Copy local config files into the image
COPY errors /usr/share/nginx/errors
COPY conf/nginx/ /etc/nginx/
COPY conf/modsec/ /etc/nginx/modsec/
COPY conf/owasp/ /usr/local/owasp-modsecurity-crs/

WORKDIR /usr/share/nginx/html

EXPOSE 80 443
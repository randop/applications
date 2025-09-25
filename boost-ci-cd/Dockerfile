FROM rfledesma/boost:latest AS builder

WORKDIR /app
COPY src src
COPY meson.build .
COPY VERSION .

RUN echo "Configuring build..." && \
    meson setup build --prefer-static --default-library=static

ENV LD_LIBRARY_PATH=/usr/local/lib

RUN echo "Compiling application..." && \
    meson compile -C build

RUN ./build/app

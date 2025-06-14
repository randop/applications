# Build stage: Use pre-built Boost C++ image
FROM rfledesma/boost:latest AS builder

WORKDIR /app
COPY . .

ENV LD_LIBRARY_PATH=/usr/local/lib

RUN echo "Configuring build..." && \
    meson setup build --prefer-static --default-library=static

RUN echo "Compiling application..." && \
    meson compile -C build

# Runtime stage: Use Debian Bookworm Slim for runtime
FROM debian:bookworm-slim

# Install runtime dependencies
RUN echo "Installing runtime packages..." && \
    apt update && apt install -y --no-install-recommends \
    libpq5 \
    openssl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create nonroot user and group
RUN echo "Configuring nonroot account..." && \
    groupadd -r nonroot && useradd -r -g nonroot -d /home/nonroot -s /sbin/nologin nonroot && \
    mkdir -p /home/nonroot && chown nonroot:nonroot /home/nonroot

# Copy compiled C++ service and healthcheck script from builder
WORKDIR /app
COPY --from=builder --chown=nonroot:nonroot /app/build/blog /app/

# Set ownership for app directory
RUN echo "Setting application directory permissions..." && \
    chown -R nonroot:nonroot /app

# Switch to nonroot user
USER nonroot

EXPOSE 10000

CMD ["/app/blog"]
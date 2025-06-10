# Build stage: Use Debian Bookworm Slim to compile C++ project and fetch s6-overlay
FROM --platform=$BUILDPLATFORM debian:bookworm-slim AS builder

# Install build tools and dependencies
RUN apt update && apt install -y --no-install-recommends \
    g++ \
    make \
    curl \
    xz-utils \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install s6-overlay for the target architecture
ARG TARGETARCH
ARG S6_OVERLAY_VERSION=3.2.1.0
RUN case "${TARGETARCH}" in \
    amd64) S6_ARCH=x86_64 ;; \
    arm64) S6_ARCH=aarch64 ;; \
    *) echo "Unsupported architecture: ${TARGETARCH}"; exit 1 ;; \
    esac && \
    echo "Fetching s6-overlay for ${S6_ARCH}" && \
    curl -sSLf --retry 3 --retry-delay 5 \
      https://github.com/just-containers/s6-overlay/releases/download/v${S6_OVERLAY_VERSION}/s6-overlay-noarch.tar.xz -o /tmp/s6-overlay-noarch.tar.xz && \
    curl -sSLf --retry 3 --retry-delay 5 \
      https://github.com/just-containers/s6-overlay/releases/download/v${S6_OVERLAY_VERSION}/s6-overlay-${S6_ARCH}.tar.xz -o /tmp/s6-overlay-${S6_ARCH}.tar.xz && \
    mkdir -p /s6-overlay && \
    tar -C /s6-overlay -Jxpf /tmp/s6-overlay-noarch.tar.xz && \
    tar -C /s6-overlay -Jxpf /tmp/s6-overlay-${S6_ARCH}.tar.xz && \
    chmod +x /s6-overlay/init && \
    rm -rf /tmp/*.tar.xz

# Copy and build the C++ project
WORKDIR /app
COPY src /app/src
COPY Makefile /app/
COPY healthcheck.sh /app/
ARG TARGETARCH
RUN make && \
    chmod +x /app/healthcheck.sh

# Runtime stage: Use Debian Bookworm Slim for runtime
FROM debian:bookworm-slim

# Install runtime dependencies for s6-overlay and healthcheck
RUN apt update && apt install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create nonroot user and group
RUN groupadd -r nonroot && useradd -r -g nonroot -d /home/nonroot -s /sbin/nologin nonroot && \
    mkdir -p /home/nonroot && chown nonroot:nonroot /home/nonroot

# Set up /run and /var/log with correct permissions for s6-overlay and app-service
RUN mkdir -p /run /var/log && \
    chown nonroot:nonroot /run /var/log && \
    chmod 755 /run /var/log

RUN touch /var/log/service.log && chown nonroot:nonroot /var/log/service.log && chmod 777 /var/log/service.log

# Copy s6-overlay from builder stage
COPY --from=builder --chown=nonroot:nonroot /s6-overlay /

# Enable read-only root support for s6-overlay
ENV S6_READ_ONLY_ROOT=1

# Copy compiled C++ service and healthcheck script from builder
WORKDIR /app
COPY --from=builder --chown=nonroot:nonroot /app/service /app/
COPY --from=builder --chown=nonroot:nonroot /app/healthcheck.sh /app/

# Copy s6-overlay service configuration from local project
COPY --chown=nonroot:nonroot resources/docker/s6-rc.d /etc/s6-overlay/s6-rc.d

# Set ownership for app directory
RUN chown -R nonroot:nonroot /app

# Switch to nonroot user
USER nonroot

# Add healthcheck
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD ["/app/healthcheck.sh"]

# Set the entrypoint to s6-overlay's init system
ENTRYPOINT ["/init"]

# Command to keep the container running
CMD ["tail", "-f", "/var/log/service.log"]
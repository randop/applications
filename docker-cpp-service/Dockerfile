# Build stage: Use Alpine to compile the C++ project and fetch s6-overlay
FROM --platform=$BUILDPLATFORM alpine:3.22 AS builder

# Install build tools and dependencies
RUN apk add --no-cache g++ make curl

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
    rm /tmp/s6-overlay*.tar.xz

# Copy and build the C++ project
WORKDIR /app
COPY src /app/src
COPY Makefile /app/
ARG TARGETARCH
RUN make

# Runtime stage: Use Distroless for minimal image
FROM gcr.io/distroless/cc-debian12:latest

# Copy s6-overlay from builder
COPY --from=builder /s6-overlay /

# Copy compiled C++ service and healthcheck script
WORKDIR /app
COPY --from=builder /app/service /app/
COPY healthcheck.sh /app/

# Set up s6 service directory
RUN mkdir -p /etc/s6-overlay/s6-rc.d/cpp-service && \
    echo "longrun" > /etc/s6-overlay/s6-rc.d/cpp-service/type && \
    echo "#!/command/execlineb -P\n/app/service" > /etc/s6-overlay/s6-rc.d/cpp-service/run && \
    chmod +x /etc/s6-overlay/s6-rc.d/cpp-service/run && \
    mkdir -p /etc/s6-overlay/s6-rc.d/user/contents.d && \
    touch /etc/s6-overlay/s6-rc.d/user/contents.d/cpp-service && \
    chmod +x /app/healthcheck.sh

# Add healthcheck (Distroless has no shell, so we rely on the service's log)
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD ["/app/healthcheck.sh"]

# Set the entrypoint to s6-overlay's init system
ENTRYPOINT ["/init"]

# Command to keep the container running
CMD []

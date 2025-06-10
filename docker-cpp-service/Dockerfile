# Use a multi-arch Alpine base image
FROM --platform=$BUILDPLATFORM alpine:3.20

# Install build tools, s6-overlay, and dependencies
RUN apk add --no-cache g++ make curl

# Install s6-overlay for the target architecture
ARG TARGETARCH
ARG S6_OVERLAY_VERSION=3.2.0.0
RUN case "${TARGETARCH}" in \
    amd64) S6_ARCH=x86_64 ;; \
    arm64) S6_ARCH=aarch64 ;; \
    *) echo "Unsupported architecture: ${TARGETARCH}"; exit 1 ;; \
    esac && \
    curl -sSL https://github.com/just-containers/s6-overlay/releases/download/v${S6_OVERLAY_VERSION}/s6-overlay-noarch.tar.xz -o /tmp/s6-overlay-noarch.tar.xz && \
    curl -sSL https://github.com/just-containers/s6-overlay/releases/download/v${S6_OVERLAY_VERSION}/s6-overlay-${S6_ARCH}.tar.xz -o /tmp/s6-overlay-${S6_ARCH}.tar.xz && \
    tar -C / -Jxpf /tmp/s6-overlay-noarch.tar.xz && \
    tar -C / -Jxpf /tmp/s6-overlay-${S6_ARCH}.tar.xz && \
    rm /tmp/s6-overlay*.tar.xz

# Copy the C++ project source code and healthcheck script
WORKDIR /app
COPY src /app/src
COPY Makefile /app/
COPY healthcheck.sh /app/

# Build the C++ project with architecture-specific optimization
ARG TARGETARCH
RUN make

# Set up s6 service directory
RUN mkdir -p /etc/s6-overlay/s6-rc.d/cpp-service && \
    echo "longrun" > /etc/s6-overlay/s6-rc.d/cpp-service/type && \
    echo "#!/command/execlineb -P\n/app/service" > /etc/s6-overlay/s6-rc.d/cpp-service/run && \
    chmod +x /etc/s6-overlay/s6-rc.d/cpp-service/run && \
    mkdir -p /etc/s6-overlay/s6-rc.d/user/contents.d && \
    touch /etc/s6-overlay/s6-rc.d/user/contents.d/cpp-service && \
    chmod +x /app/healthcheck.sh

# Clean up build dependencies to reduce image size
RUN apk del g++ make curl && \
    rm -rf /var/cache/apk/*

# Add healthcheck
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD /app/healthcheck.sh

# Set the entrypoint to s6-overlay's init system
ENTRYPOINT ["/init"]

# Command to keep the container running
CMD []

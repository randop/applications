# Deployment snippets

## Docker

Build the image on Linux:

```bash
docker build -t websrv:latest .
docker run --rm \
  -p 8443:8443/udp \
  -p 8443:8443/tcp \
  -p 8080:8080/tcp \
  --mount type=bind,src="$PWD/certs/fullchain.pem",dst=/run/secrets/tls_cert,readonly \
  --mount type=bind,src="$PWD/certs/privkey.pem",dst=/run/secrets/tls_key,readonly \
  websrv:latest
```

The image contains `/app/config.yaml`, copied from `deploy/config.docker.yaml`. To use a different configuration, mount it at `/app/config.yaml` or override the command with `--config /path/to/config.yaml`. Ensure the private key is readable by the container user (UID 10001). Publish UDP/8443 for HTTP/3, TCP/8443 for HTTPS with HTTP/2/HTTP/1.1 ALPN, and optionally TCP/8080 for plain HTTP/1.1; do not publish port 8080 unless cleartext traffic is intended to be reachable.

**io_uring is mandatory.** Do not run with `--privileged` as a workaround if the default seccomp profile blocks it; use a reviewed seccomp profile that permits `io_uring_setup`, `io_uring_enter`, and required registration calls. Startup intentionally fails when ring creation is denied. This image serves HTTP/3 over UDP and, by default, HTTPS with HTTP/2 and HTTP/1.1 negotiated over TCP port 8443, plus plain HTTP/1.1 over TCP port 8080. All three listeners use Monoio's required io_uring driver; the TLS listener negotiates HTTP/2 or HTTP/1.1 with ALPN. Set `tcp_http1_enabled: false` to disable the TLS TCP listener, `http2_enabled: false` to disable HTTP/2 ALPN while retaining HTTPS/1.1, or `http1_cleartext_enabled: false` to disable cleartext HTTP/1.1.

## systemd

1. Create a dedicated `websrv` system user and group.
2. Install the release binary at `/opt/websrv/bin/websrv`, configuration at `/opt/websrv/config.yaml`, and the static roots configured in YAML.
3. Set `tls_cert` and `tls_key` in YAML. Make the configuration readable by `websrv`; keep the private key readable only by the service account and administrators.
4. Install `websrv.service` to `/etc/systemd/system/`, then run `systemctl daemon-reload && systemctl enable --now websrv`.
5. Ensure the service user can read certificate files and all configured static roots. Configure certificate renewal hooks to restart the process after rotation.
6. Confirm the service's seccomp/system-call policy allows io_uring. Do not add a policy that blocks `io_uring_setup`, `io_uring_enter`, or `io_uring_register`.

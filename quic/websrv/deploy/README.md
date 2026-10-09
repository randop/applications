# Deployment snippets

## Docker

Build the image on Linux:

```bash
docker build -t websrv:latest .
docker run --rm \
  -p 8443:8443/udp \
  -p 8443:8443/tcp \
  --mount type=bind,src="$PWD/certs/fullchain.pem",dst=/run/secrets/tls_cert,readonly \
  --mount type=bind,src="$PWD/certs/privkey.pem",dst=/run/secrets/tls_key,readonly \
  websrv:latest
```

The image contains `/app/config.yaml`, copied from `deploy/config.docker.yaml`. To use a different configuration, mount it at `/app/config.yaml` or override the command with `--config /path/to/config.yaml`. Ensure the private key is readable by the container user (UID 10001) and keep UDP/8443 (HTTP/3) and TCP/8443 (HTTPS/1.1, if enabled) open in firewalls and load balancers.

**io_uring is mandatory.** Do not run with `--privileged` as a workaround if the default seccomp profile blocks it; use a reviewed seccomp profile that permits `io_uring_setup`, `io_uring_enter`, and required registration calls. Startup intentionally fails when ring creation is denied. This image serves HTTP/3 over UDP and, by default, HTTPS/1.1 over TCP on the same numeric port; both transports use Monoio's required io_uring driver. Set `tcp_http1_enabled: false` to disable the TCP protocol fallback.

## systemd

1. Create a dedicated `websrv` system user and group.
2. Install the release binary at `/opt/websrv/bin/websrv`, configuration at `/opt/websrv/config.yaml`, and the static roots configured in YAML.
3. Set `tls_cert` and `tls_key` in YAML. Make the configuration readable by `websrv`; keep the private key readable only by the service account and administrators.
4. Install `websrv.service` to `/etc/systemd/system/`, then run `systemctl daemon-reload && systemctl enable --now websrv`.
5. Ensure the service user can read certificate files and all configured static roots. Configure certificate renewal hooks to restart the process after rotation.
6. Confirm the service's seccomp/system-call policy allows io_uring. Do not add a policy that blocks `io_uring_setup`, `io_uring_enter`, or `io_uring_register`.

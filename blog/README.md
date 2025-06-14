# BLOG
Blog server project written in C++

## Dependencies
1. `libpq`
* Debian / Ubuntu: `apt install libpq-dev`
* Fedora / Redhat: `dnf install libpq-devel`

## Development
```bash
docker compose up --renew-anon-volumes
```

### Configure the Build
```bash
meson setup build --prefer-static --default-library=static
```

### Compile
```bash
meson compile -C build
```

## Build and Deploy
```bash
docker buildx create --name multiarch --use
docker buildx build --platform linux/amd64,linux/arm64 -t rfledesma/blog:latest --push .
```
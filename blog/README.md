# BLOG
Blog server project written in C++

## Dependencies
1. `libpq`
* Debian / Ubuntu: `apt install libpq-dev`
* Fedora / Redhat: `dnf install libpq-devel`

## Development
```bash
docker compose up
```

### Configure the Build
```bash
meson setup build --prefer-static --default-library=static
```

### Compile
```bash
meson compile -C build
```
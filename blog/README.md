# BLOG
Blog server project written in C++

## Dependencies
1. `libpq`
* Debian / Ubuntu: `apt install libpq-dev`
* Fedora / Redhat: `dnf install libpq-devel`
2. `libmongoc` and `libbson`
```bash
sudo mkdir -p /opt/mongo-c-driver/current
sudo git clone -b v2.0.2 --depth 1 https://github.com/mongodb/mongo-c-driver.git /opt/mongo-c-driver/2.0.2
sudo cd /opt/mongo-c-driver/2.0.2 && cmake -DCMAKE_INSTALL_PREFIX=/opt/mongo-c-driver/current .
sudo cd /opt/mongo-c-driver/2.0.2 && make all install
sudo echo "/opt/mongo-c-driver/current" > /etc/ld.so.conf.d/boost.conf
sudo ldconfig
```

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
# QUIC Reverse Proxy

### Building BoringSSL
```sh
mkdir -p /opt/boringssl/current
cd /opt/boringssl
git clone https://boringssl.googlesource.com/boringssl 9fc1c33e9c21439ce5f87855a6591a9324e569fd
cd /opt/boringssl/9fc1c33e9c21439ce5f87855a6591a9324e569fd
git checkout 9fc1c33e9c21439ce5f87855a6591a9324e569fd
mkdir build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_INSTALL_PREFIX=/opt/boringssl/current \
  -DCMAKE_C_FLAGS="-Wno-dangling-pointer -Wno-ignored-attributes" \
  -DCMAKE_CXX_FLAGS="-Wno-dangling-pointer -Wno-ignored-attributes"

make -j$(nproc)
make install
```

### Setup lsquic
```sh
# cd to project root first
mkdir -p subprojects/lsquic
git clone https://github.com/litespeedtech/lsquic.git subprojects/lsquic
cd subprojects/lsquic
git checkout 7e0c302ed8271c0019ea1155ebc9357bf4973f42
git submodule update --init --recursive
```
```
```

### Generate Certificates
```sh
# cd to project root first
mkdir certs && cd certs
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes -subj "/C=US/ST=State/L=City/O=Organization/CN=localhost"

```

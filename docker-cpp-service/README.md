# C++ service using docker and s6

## Structure
```
project/
├── Dockerfile
├── Makefile
├── healthcheck.sh
└── src/
    └── main.cpp
```

## Build
```bash
docker buildx create --name multiarch --use
docker buildx build --platform linux/amd64,linux/arm64 -t randop/cpp-service:latest --push .
```

# C++ service using docker and s6

## Structure
```
project/
├── resources/
│   └── docker/
│       └── s6-rc.d/
│           ├── app-service/
│           │   ├── run
│           │   └── type
│           └── user/
│               └── contents.d/
│                   └── app-service
├── src/
├── Makefile
├── healthcheck.sh
└── Dockerfile
```

## Build
```bash
docker buildx create --name multiarch --use
docker buildx build --platform linux/amd64,linux/arm64 -t randop/cpp-service:latest --push .
```

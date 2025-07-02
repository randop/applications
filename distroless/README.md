# distroless
Experimental image for developing secure container

## Build image
```bash
docker build -t distroless .
```

## Run shell
```bash
docker run -it --rm distroless /bin/busybox sh
```
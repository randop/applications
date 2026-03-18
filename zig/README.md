# Zig

## Setup
```sh
export ZIG_PACKAGE=https://ziglang.org/builds/zig-x86_64-linux-0.16.0-dev.2905+5d71e3051.tar.xz
rm -rf /tmp/zig
mkdir -p /tmp/zig
wget -O /tmp/zig/zig-linux.tar.xz $ZIG_PACKAGE
tar -xf /tmp/zig/zig-linux.tar.xz -C /tmp/zig
mv -v /tmp/zig/zig ~/.local/bin
mv -v /tmp/zig/lib ~/.local/
rm -rf /tmp/zig
chmod +x ~/.local/bin/zig

# Check
zig version
# 0.16.0-dev.2905+5d71e3051
```

## Hello World
```sh
zig build-exe hello.zig
```

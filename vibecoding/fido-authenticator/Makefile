# Build with the Luckfox SDK's Buildroot toolchain so the binary matches the
# target's libc (uclibc/musl/glibc depending on your SDK image) and kernel
# headers. Example (adjust prefix to whatever your SDK's
# tools/linux/toolchain/ directory actually contains):
#
#   export PATH=$PATH:/path/to/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin
#   make CROSS=arm-rockchip830-linux-uclibcgnueabihf-
#
# You also need OpenSSL built for the target. If the Luckfox Buildroot config
# doesn't already include it: `make menuconfig` in the SDK, enable
# BR2_PACKAGE_OPENSSL (Target packages -> Libraries -> Crypto), rebuild the
# rootfs/toolchain sysroot, then point CFLAGS/LDFLAGS at it if it's not
# already on the toolchain's default include/lib path.

CROSS ?=
CXX = $(CROSS)g++
CXXFLAGS = -std=c++17 -O2 -Wall -Isrc
LDFLAGS = -lssl -lcrypto -lpthread

SRCS = src/main.cpp src/crypto.cpp src/u2f_protocol.cpp src/ctaphid_transport.cpp
OBJS = $(SRCS:.cpp=.o)
BIN = fido-authenticator

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean

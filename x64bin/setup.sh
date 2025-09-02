#!/usr/bin/env bash
apt update && apt install clang llvm

# EDK II
apt install build-essential uuid-dev iasl git nasm python-is-python3

# Download picolibc
mkdir -p /opt/picolibc
wget -O /opt/picolibc/picolibc-1.8.10-14.2.rel1.zip "https://github.com/picolibc/picolibc/releases/download/1.8.10/picolibc-1.8.10-14.2.rel1.zip"
unzip /opt/picolibc/picolibc-1.8.10-14.2.rel1.zip -d /opt/picolibc

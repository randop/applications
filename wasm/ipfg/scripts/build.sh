#!/bin/sh

mkdir -p ./.build
rm -fv ip_ranges.o ip_strings.o ./.build/ipfg

# Force clean symbols
objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
  --redefine-sym _binary_data_ip_ranges_bin_start=_binary_ip_ranges_bin_start \
  --redefine-sym _binary_data_ip_ranges_bin_end=_binary_ip_ranges_bin_end \
  --redefine-sym _binary_data_ip_ranges_bin_size=_binary_ip_ranges_bin_size \
  data/ip_ranges.bin ip_ranges_elf86.o

objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
  --rename-section .data=.rodata,alloc,load,readonly,data,contents \
  --redefine-sym _binary_data_ip_strings_bin_start=_binary_ip_strings_bin_start \
  --redefine-sym _binary_data_ip_strings_bin_end=_binary_ip_strings_bin_end \
  --redefine-sym _binary_data_ip_strings_bin_size=_binary_ip_strings_bin_size \
  data/ip_strings.bin ip_strings_elf86.o

nm ip_ranges.o | grep _binary
nm ip_strings.o | grep _binary

echo "Compiling..." && g++ -std=c++20 -O3 -march=native -fno-pie -no-pie -mcmodel=medium main.cpp ip_ranges_elf86.o ip_strings_elf86.o -o ./build/ipfg

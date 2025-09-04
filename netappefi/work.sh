#!/usr/bin/env bash

set -euo pipefail

export CC=clang
export CXX=clang++
export LD=ld.lld

export WORKSPACE=/work/git/tianocore
mkdir -p $WORKSPACE
cd $WORKSPACE

git clone https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init

cd $WORKSPACE
git clone https://github.com/tianocore/edk2-platforms.git
cd edk2-platforms
git submodule update --init

cd $WORKSPACE
git clone https://github.com/tianocore/edk2-non-osi.git

. edk2/edksetup.sh
make -C edk2/BaseTools

# Edit configurations
vim edk2/Conf/target.txt
vim MdeModulePkg/MdeModulePkg.dsc

# Test build process
build -p ShellPkg/ShellPkg.dsc -a X64 -t CLANGPDB -b RELEASE -n $(nproc)

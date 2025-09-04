# Network Application EFI

## Requirements
* Arch Linux (Linux arch 6.8.12-9-pve #1 SMP PREEMPT_DYNAMIC PMX 6.8.12-9 (2025-03-16T19:18Z) x86_64 GNU/Linux)
* Toolchain
```sh
pacman -S base-devel git util-linux acpica nasm python clang llvm lld libc++ vim
```

## Guide
[https://github.com/tianocore/edk2-platforms?tab=readme-ov-file#how-to-build-linux-environment](https://github.com/tianocore/edk2-platforms?tab=readme-ov-file#how-to-build-linux-environment)
## Build Process
```sh
export WORKSPACE=/work/git/tianocore
cd $WORKSPACE
export PACKAGES_PATH=$PWD/edk2:$PWD/edk2-platforms:$PWD/edk2-non-osi
. edk2/edksetup.sh
cd /work/git/tianocore/edk2
build -a X64 -t CLANGDWARF -p MdeModulePkg/MdeModulePkg.dsc -m MdeModulePkg/Application/NetworkHelloWorld/NetworkHelloWorld.in
```

## Output
```
/work/git/tianocore/Build/MdeModule/RELEASE_CLANGDWARF/X64/NetworkHelloWorld.efi
```

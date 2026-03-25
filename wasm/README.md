# WASM

**How the `ipfg` WASM binary ended up with a broken `./ipfg` executable — and exactly how to fix it**

A static WASM binary compiled with `wasi-sdk` + musl (`wasm32-wasi` target) is perfectly portable. The same `ipfg.wasm` file runs on x86_64, aarch64, and armv7 without recompilation. The problems only appeared when trying to turn the AOT-compiled output into a direct `./ipfg` executable.

### What went wrong, step by step

1. **The compile command produced the wrong kind of file**  
   `wasmtime compile ipfg.wasm -o ipfg` created an ELF 64-bit LSB relocatable x86-64 object (exactly what `file ipfg` showed: “ELF 64-bit LSB relocatable, x86-64, version 1, not stripped”).  
   This is an internal pre-compiled module (`.cwasm` format), **not** a standalone executable with a proper ELF header, entry point, and interpreter that the Linux kernel can launch directly. That is why `./ipfg` immediately failed with “Failed to execute process … Maybe the interpreter directive (#! line) is broken?”

2. **Running the pre-compiled file without the required flag**  
   Even when the filename was corrected, `wasmtime run --dir=. ipfg -- hello` produced:  
   “Error: running a precompiled module requires the `--allow-precompiled` flag”.  
   Wasmtime disables pre-compiled modules by default for security (they contain native code tied to a specific CPU and Wasmtime version). Without the explicit flag, execution is blocked.

3. **Shebang / wrapper mistakes**  
   Attempts to run the raw object file or to add a shebang directly on it were doomed because the file was never a real executable in the first place.

### The correct ways to run it (wasmtime 43.0)

#### Option 1 – Simplest and most reliable (no AOT)
```bash
wasmtime run --dir=. ipfg.wasm -- parameter1 parameter2
```
- No compilation step.
- Works immediately on x86_64, aarch64, and (via interpreter) on armv7.
- `--dir=.` gives the program access to files in the current directory (add more `--dir=/path` as needed).

#### Option 2 – AOT pre-compilation (faster startup) with correct usage
```bash
# Compile once (use a clear extension)
wasmtime compile ipfg.wasm -o ipfg.cwasm

# Run the pre-compiled module
wasmtime run --allow-precompiled --dir=. ipfg.cwasm -- parameter1 parameter2
```

#### Option 3 – Convenient wrapper so you can just type `./ipfg`
Create the wrapper once (on the target machine):

```bash
cat > ipfg << 'EOF'
#!/usr/bin/env bash
# Wrapper for ipfg pre-compiled with wasmtime
exec wasmtime run --allow-precompiled --dir=. "$(dirname "$0")/ipfg.cwasm" -- "$@"
EOF

chmod +x ipfg
```

Now run simply with:
```bash
./ipfg
./ipfg parameter1 parameter2 --flag value
```

Keep both `ipfg.wasm` (or the original) and `ipfg.cwasm` in the same directory as the wrapper.

### Full cross-platform instructions (x86_64, aarch64, armv7)

**x86_64 and aarch64 (arm64)** – Use Wasmtime everywhere  
- Install: `curl https://wasmtime.dev/install.sh -sSf | bash` (auto-detects architecture).  
- Follow Option 1, 2, or 3 above.  
- The pre-compiled `.cwasm` file is specific to that CPU; rebuild it on each target architecture if distributing AOT files.

**armv7 / 32-bit ARM** (e.g. older Raspberry Pi)  
Wasmtime does not ship ready 32-bit ARM binaries, so use WAMR (`iwasm`):

```bash
# One-time build on the ARM32 device
sudo apt update && sudo apt install git cmake build-essential -y
git clone --recursive https://github.com/bytecodealliance/wasm-micro-runtime.git
cd wasm-micro-runtime/product-mini/platforms/linux
mkdir build && cd build
cmake .. -DWAMR_BUILD_TARGET=ARMV7
make -j$(nproc)
```

Then run:
```bash
./iwasm --dir=. /path/to/ipfg.wasm -- parameter1 parameter2
```

(Optional) WAMR also supports its own AOT mode via `wamrc`.

### Examining wasm binary
```sh
file ipfg.wasm
```
> ipfg.wasm: WebAssembly (wasm) binary module version 0x1 (MVP)
```sh
ldd ipfg.wasm
```
> not a dynamic executable
```sh
file ipfg
```
> ipfg: ELF 64-bit LSB relocatable, x86-64, version 1, not stripped
```sh
ldd ipfg
```
> not a dynamic executable

### Quick reference

| Platform     | Runtime   | Recommended command                                      | Needs rebuild per machine? |
|--------------|-----------|----------------------------------------------------------|----------------------------|
| x86_64       | Wasmtime  | `wasmtime run --dir=. ipfg.wasm …` or wrapper            | No for .wasm; yes for .cwasm |
| aarch64      | Wasmtime  | same as above                                            | No for .wasm; yes for .cwasm |
| armv7 (32-bit) | WAMR    | `./iwasm --dir=. ipfg.wasm …`                            | No                         |

### Final checklist to avoid the same errors again

- Never run `./` directly on the output of `wasmtime compile`.  
- Always use `wasmtime run --allow-precompiled …` for pre-compiled files, or simply run the original `.wasm`.  
- Use a small bash wrapper if you want a clean `./ipfg` command.  
- Distribute the original `ipfg.wasm` for maximum portability; create platform-specific `.cwasm` files only where startup speed matters.  
- Verify with `wasmtime --version` and `file ipfg.wasm` (should report WebAssembly binary).

With these steps the same static WASM binary runs cleanly on x86, arm64, and 32-bit arm without further issues.

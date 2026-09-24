# rust

## setup
```bash
mkdir -pv $HOME/projects/toolchains/rust
cd $HOME/projects/toolchains/rust
export CARGO_HOME="$HOME/projects/toolchains/rust/cargo"
export RUSTUP_HOME="$HOME/projects/toolchains/rust/rustup"
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | \
                    sh -s -- -y --no-modify-path --default-toolchain stable
```
```
info: downloading installer
info: profile set to default
info: default host tuple is x86_64-unknown-linux-gnu
info: syncing channel updates for stable-x86_64-unknown-linux-gnu
info: latest update on 2026-09-03 for version 1.98.1 (48a229cea 2026-09-01)
info: downloading 6 components
        cargo installed                       11.12 MiB                                                                                                   clippy installed                        5.15 MiB                                                                                                rust-docs installed                       22.96 MiB                                                                                                 rust-std installed                       29.29 MiB                                                                                                    rustc installed                       76.03 MiB                                                                                                  rustfmt installed                        2.37 MiB                                                                                            info: default toolchain set to stable-x86_64-unknown-linux-gnu

  stable-x86_64-unknown-linux-gnu installed - rustc 1.98.1 (48a229cea 2026-09-01)


Rust is installed now. Great!

To get started you need Cargo's bin directory 
(/home/devsecops/projects/toolchains/rust/cargo/bin) in your PATH
environment variable. This has not been done automatically.

To configure your current shell, you need to source
the corresponding env file under /home/devsecops/projects/toolchains/rust/cargo.

Consider running the right command for your shell (note the leading DOT):
. "/home/devsecops/projects/toolchains/rust/cargo/env"           # For 
sh/ash/dash/pdksh/bash
source "/home/devsecops/projects/toolchains/rust/cargo/env.fish" # For fish
cargo:rerun-if-env-changed=CC_x86_64-unknown-linux-gnu
CC_x86_64-unknown-linux-gnu = None
cargo:rerun-if-env-changed=CC_x86_64_unknown_linux_gnu
CC_x86_64_unknown_linux_gnu = None
cargo:rerun-if-env-changed=HOST_CC
HOST_CC = None
cargo:rerun-if-env-changed=CC
CC = None
cargo:rerun-if-env-changed=CC_ENABLE_DEBUG_OUTPUT
cargo:rerun-if-env-changed=CRATE_CC_NO_DEFAULTS
CRATE_CC_NO_DEFAULTS = None
cargo:rerun-if-env-changed=CFLAGS
CFLAGS = None
cargo:rerun-if-env-changed=HOST_CFLAGS
HOST_CFLAGS = None
cargo:rerun-if-env-changed=CFLAGS_x86_64_unknown_linux_gnu
CFLAGS_x86_64_unknown_linux_gnu = None
cargo:rerun-if-env-changed=CFLAGS_x86_64-unknown-linux-gnu
CFLAGS_x86_64-unknown-linux-gnu = None
```

## configure linker
```bash
mkdir -pv $HOME/projects/toolchains/rust/mold
cd $HOME/projects/toolchains/rust/mold
wget "https://github.com/rui314/mold/releases/download/v2.42.1/mold-2.42.1-x86_64-linux.tar.gz"
tar xzvf mold-2.42.1-x86_64-linux.tar.gz --strip-components=1
ln -sv $HOME/projects/toolchains/rust/mold/bin/mold $HOME/projects/toolchains/rust/cargo/bin/mold

cat > "$CARGO_HOME/config.toml" << EOF
[target.x86_64-unknown-linux-gnu]
linker = "mold"
rustflags = ["-C", "link-arg=-fuse-ld=mold"]
EOF
```

## configure flatpak and opencode
```bash
flatpak override --user \
  --env=CARGO_HOME=$HOME/projects/toolchains/rust/cargo \
  --env=RUSTUP_HOME=$HOME/projects/toolchains/rust/rustup \
  --env=PATH=/app/bin:/usr/bin:$HOME/projects/toolchains/rust/cargo/bin \
  ai.opencode.opencode
```

#!/bin/sh

# Mimic Bash's `set -eou pipefail` as closely as possible in pure POSIX sh

set -e # errexit: exit immediately if a command exits with non-zero status
set -u # nounset: treat unset variables as an error (and exit)

# pipefail: make pipelines fail if any command fails (not in POSIX)
# Fallback that works in most real-world /bin/sh (including when sh is Bash)
if command -v set >/dev/null 2>&1 && set -o pipefail 2>/dev/null; then
  set -o pipefail
else
  # Pure POSIX fallback (not perfect, but better than nothing)
  # It makes the whole pipeline's exit status come from the last command that failed
  # by using a simple wrapper when needed.
  pipefail() {
    "$@"
    _status=$?
    if [ $_status -ne 0 ]; then
      return $_status
    fi
  }
fi

rm -fv ./.build-wasm/ipfg.wasm
meson compile -C .build-wasm

wasmtime ./.build-wasm/ipfg.wasm <<'EOF'
192.168.1.1
EOF

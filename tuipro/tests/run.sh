#!/usr/bin/env bash
# Run the tuipro test suite.
#
#   ./tests/run.sh
#   TUIPRO_BIN=build/tuipro ./tests/run.sh
#   TUIPRO_PTY=0 ./tests/run.sh        # skip the pseudo-terminal smoke test
set -u

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN=${TUIPRO_BIN:-$ROOT/build/tuipro}
TESTS=("$ROOT"/tests/test_*.lua)

if [ ! -x "$BIN" ]; then
  echo "tests/run.sh: tuipro binary not found at $BIN" >&2
  echo "build it with: cmake -S . -B build && cmake --build build -j" >&2
  exit 2
fi

cd "$ROOT" || exit 2

failed=0
ran=0

for test in "${TESTS[@]}"; do
  [ -e "$test" ] || continue
  ran=$((ran + 1))
  name=${test#"$ROOT"/}
  echo "== $name"
  if command -v timeout >/dev/null 2>&1; then
    timeout 120 "$BIN" --headless --size 100x30 "$test"
  else
    "$BIN" --headless --size 100x30 "$test"
  fi
  status=$?
  if [ "$status" -ne 0 ]; then
    echo "FAILED $name (exit $status)" >&2
    failed=$((failed + 1))
  fi
done

if [ "$ran" -eq 0 ]; then
  echo "tests/run.sh: no test files found" >&2
  exit 2
fi

# CLI-level smoke checks: built-in demo, plugin entry point and exit codes.
run_cli() {
  local desc=$1 expected=$2
  shift 2
  local out status
  if command -v timeout >/dev/null 2>&1; then
    out=$(timeout 60 "$@" 2>&1)
  else
    out=$("$@" 2>&1)
  fi
  status=$?
  if [ "$status" -ne "$expected" ]; then
    echo "FAILED CLI $desc (expected exit $expected, got $status)" >&2
    [ -n "$out" ] && printf '%s\n' "$out" >&2
    failed=$((failed + 1))
  else
    echo "ok   CLI $desc"
  fi
}

echo "== CLI smoke checks"
run_cli "demo --frames 1" 0 "$BIN" --headless --size 80x24 --frames 1 --demo
run_cli "demo-coding --frames 1" 0 "$BIN" --headless --size 80x24 --frames 1 --demo-coding
run_cli "plugin --frames 1" 0 "$BIN" --headless --size 80x24 --frames 1 --plugin plugins/hello
run_cli "eval nonzero exit" 3 "$BIN" --headless -e "return 3"
run_cli "missing script" 1 "$BIN" --headless /no/such/tuipro-script.lua

for example in "$ROOT"/examples/*.lua; do
  [ -e "$example" ] || continue
  run_cli "example $(basename "$example")" 0 \
    "$BIN" --headless --size 100x30 --frames 1 "$example"
done

if [ "${TUIPRO_PTY:-1}" != "0" ]; then
  if command -v python3 >/dev/null 2>&1; then
    echo "== tests/pty_smoke.py"
    if ! TUIPRO_BIN="$BIN" python3 "$ROOT/tests/pty_smoke.py"; then
      echo "FAILED tests/pty_smoke.py" >&2
      failed=$((failed + 1))
    fi
  else
    echo "tests/run.sh: python3 not found; skipping PTY smoke test" >&2
  fi
fi

echo
if [ "$failed" -eq 0 ]; then
  echo "All $ran Lua test files and PTY checks passed."
  exit 0
fi
echo "$failed test file(s) failed." >&2
exit 1

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

# ---------------------------------------------------------------------------
# HTTP/HTTPS fixture servers for tests/test_http.lua
# ---------------------------------------------------------------------------
HTTP_FIXTURE_DIR=""
HTTP_FIXTURE_PID=""
HTTPS_FIXTURE_PID=""

cleanup_fixtures() {
  [ -n "$HTTP_FIXTURE_PID" ] && kill "$HTTP_FIXTURE_PID" 2>/dev/null
  [ -n "$HTTPS_FIXTURE_PID" ] && kill "$HTTPS_FIXTURE_PID" 2>/dev/null
  [ -n "$HTTP_FIXTURE_DIR" ] && rm -rf "$HTTP_FIXTURE_DIR"
}
trap cleanup_fixtures EXIT INT TERM

wait_for_port() {
  local file=$1 pid=$2 tries=100 port
  while [ "$tries" -gt 0 ]; do
    port=$(awk '/^PORT /{print $2; exit}' "$file" 2>/dev/null)
    if [ -n "$port" ]; then
      printf '%s' "$port"
      return 0
    fi
    kill -0 "$pid" 2>/dev/null || return 1
    tries=$((tries - 1))
    sleep 0.05
  done
  return 1
}

if command -v python3 >/dev/null 2>&1; then
  HTTP_FIXTURE_DIR=$(mktemp -d)
  python3 "$ROOT/tests/http_server.py" --port 0 \
    >"$HTTP_FIXTURE_DIR/http.out" 2>"$HTTP_FIXTURE_DIR/http.err" &
  HTTP_FIXTURE_PID=$!
  HTTP_PORT=$(wait_for_port "$HTTP_FIXTURE_DIR/http.out" "$HTTP_FIXTURE_PID") || HTTP_PORT=""
  if [ -n "$HTTP_PORT" ]; then
    export TUIPRO_HTTP_PORT="$HTTP_PORT"
  else
    echo "tests/run.sh: could not start the HTTP fixture server" >&2
    cat "$HTTP_FIXTURE_DIR/http.err" >&2 2>/dev/null
  fi

  if [ -n "$HTTP_PORT" ] && command -v openssl >/dev/null 2>&1; then
    openssl req -x509 -newkey rsa:2048 -nodes -days 2 \
      -keyout "$HTTP_FIXTURE_DIR/key.pem" \
      -out "$HTTP_FIXTURE_DIR/cert.pem" \
      -subj "/CN=localhost" \
      -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
      >/dev/null 2>&1
    if [ -s "$HTTP_FIXTURE_DIR/cert.pem" ]; then
      python3 "$ROOT/tests/http_server.py" --port 0 \
        --cert "$HTTP_FIXTURE_DIR/cert.pem" \
        --key "$HTTP_FIXTURE_DIR/key.pem" \
        >"$HTTP_FIXTURE_DIR/https.out" 2>"$HTTP_FIXTURE_DIR/https.err" &
      HTTPS_FIXTURE_PID=$!
      HTTPS_PORT=$(wait_for_port "$HTTP_FIXTURE_DIR/https.out" \
        "$HTTPS_FIXTURE_PID") || HTTPS_PORT=""
      if [ -n "$HTTPS_PORT" ]; then
        export TUIPRO_HTTPS_PORT="$HTTPS_PORT"
      else
        echo "tests/run.sh: could not start the HTTPS fixture server" >&2
        cat "$HTTP_FIXTURE_DIR/https.err" >&2 2>/dev/null
      fi
    fi
  fi
else
  echo "tests/run.sh: python3 not found; network tests will be skipped" >&2
fi

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
run_cli "demo-api --frames 1" 0 "$BIN" --headless --size 80x24 --frames 1 --demo-api
run_cli "demo-theme --frames 1" 0 "$BIN" --headless --size 80x24 --frames 1 --demo-theme
run_cli "plugin --frames 1" 0 "$BIN" --headless --size 80x24 --frames 1 --plugin plugins/hello
run_cli "theme plugin --frames 1" 0 "$BIN" --headless --size 80x24 --frames 1 --plugin plugins/aurora
run_cli "opencode plugin --frames 1" 0 "$BIN" --headless --size 80x24 --frames 1 --plugin plugins/opencode
run_cli "tokyo-night plugin --frames 1" 0 "$BIN" --headless --size 80x24 --frames 1 --plugin plugins/tokyo-night
run_cli "eval nonzero exit" 3 "$BIN" --headless -e "return 3"
run_cli "missing script" 1 "$BIN" --headless /no/such/tuipro-script.lua
run_cli "theme option" 0 "$BIN" --headless --size 80x24 --frames 1 --theme tuipro-light --demo
run_cli "theme option (plugin theme)" 0 \
  "$BIN" --headless --size 80x24 --frames 1 --theme tokyo-night --demo-theme
run_cli "theme option (opencode)" 0 \
  "$BIN" --headless --size 80x24 --frames 1 --theme opencode --demo-theme
run_cli "theme option (--theme=NAME)" 0 \
  "$BIN" --headless --size 80x24 --frames 1 --theme=tuipro-light --demo
run_cli "unknown theme still runs" 0 \
  "$BIN" --headless --size 80x24 --frames 1 --theme not-a-theme --demo
run_cli "theme option needs a name" 2 "$BIN" --headless --theme

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

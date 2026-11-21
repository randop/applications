#!/usr/bin/env bash
# Orchestrates the ATS API test suite:
#   - builds the project
#   - boots the server on an isolated temporary SQLite database
#   - waits for /health readiness
#   - runs every test script in this directory
#   - tears the server and temp files down on exit
#
# Usage:  bash tests/run.sh
#         PORT=18080 bash tests/run.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PORT="${PORT:-8089}"
export BASE_URL="${BASE_URL:-http://127.0.0.1:${PORT}}"
export JWT_SECRET="${JWT_SECRET:-test-secret}"

WORKDIR="$(mktemp -d)"
DB="${WORKDIR}/ats_test.db"
LOG="${WORKDIR}/server.log"
SERVER_PID=""

cleanup() {
  if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi
  rm -rf "${WORKDIR}"
}
trap cleanup EXIT

echo "==> Building"
cargo build --quiet

echo "==> Starting server on ${BASE_URL} (db: ${DB})"
env PORT="${PORT}" \
    DATABASE_URL="sqlite://${DB}?mode=rwc" \
    JWT_SECRET="${JWT_SECRET}" \
    ./target/debug/ats >"${LOG}" 2>&1 &
SERVER_PID=$!

echo "==> Waiting for readiness"
ready=0
for _ in $(seq 1 60); do
  if curl -sf "${BASE_URL}/health" >/dev/null 2>&1; then
    ready=1
    break
  fi
  if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "server exited unexpectedly; log:" >&2
    cat "${LOG}" >&2
    exit 1
  fi
  sleep 0.25
done

if [[ "${ready}" -ne 1 ]]; then
  echo "server did not become ready; log:" >&2
  cat "${LOG}" >&2
  exit 1
fi

run() {
  echo
  echo "############################################################"
  echo "# $1"
  echo "############################################################"
  bash "$1"
}

run "${ROOT}/tests/smoke.sh"
run "${ROOT}/tests/authz.sh"
run "${ROOT}/tests/mutations.sh"
run "${ROOT}/tests/graphql.sh"

echo
echo "==> All test scripts completed"
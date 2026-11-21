#!/usr/bin/env bash
#
# Start the ATS backend in development mode.
#
# Usage:
#   scripts/dev.sh              # debug build
#   scripts/dev.sh --release    # optimized build
#
# Environment variables already set in your shell take precedence over .env.
# The server runs in the foreground; press Ctrl-C to stop it.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Load .env for any variable that is not already set in the environment.
if [[ -f "$ROOT/.env" ]]; then
  while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    [[ -z "$line" || "$line" == \#* || "$line" != *=* ]] && continue
    key="${line%%=*}"
    value="${line#*=}"
    key="${key%"${key##*[![:space:]]}"}"
    if [[ ! -v "$key" ]]; then
      export "$key=$value"
    fi
  done < "$ROOT/.env"
fi

PORT="${PORT:-8080}"
export PORT

# Listen on both IPv4 (0.0.0.0) and IPv6 ([::]) by default. Override with
# BIND_ADDR (single socket address) or BIND_ADDRS (comma-separated list).
if [[ -z "${BIND_ADDRS:-}" && -z "${BIND_ADDR:-}" ]]; then
  export BIND_ADDRS="0.0.0.0:${PORT},[::]:${PORT}"
fi

export RUST_LOG="${RUST_LOG:-ats=debug,tower_http=debug,sqlx=warn}"

echo "==> ATS development backend"
echo "    port:     $PORT"
echo "    bind:     ${BIND_ADDRS:-${BIND_ADDR:-}}"
echo "    database: ${DATABASE_URL:-sqlite://ats.db?mode=rwc}"
echo "    logs:     $RUST_LOG"
echo

if [[ "${1:-}" == "--release" ]]; then
  cargo build --release
  exec "$ROOT/target/release/ats"
else
  cargo build
  exec "$ROOT/target/debug/ats"
fi
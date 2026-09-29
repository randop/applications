#!/usr/bin/env bash
set -euo pipefail

MIN_VERSION="0.21.0"

# Extract semver string (e.g., "0.21.0") from 'hermes --version'
CURRENT_VERSION=$(hermes --version 2>/dev/null | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+' | tr -d 'v' || true)

if [ -z "$CURRENT_VERSION" ]; then
  echo "Error: Could not determine Hermes version."
  exit 1
fi

# Check if CURRENT_VERSION >= MIN_VERSION using sort -V
OLDEST_VERSION=$(printf '%s\n%s\n' "$MIN_VERSION" "$CURRENT_VERSION" | sort -V | head -n1)

if [ "$OLDEST_VERSION" != "$MIN_VERSION" ]; then
  echo "Error: Hermes v$CURRENT_VERSION is installed, but v$MIN_VERSION or higher is required."
  exit 1
fi

echo "Version check passed: Hermes v$CURRENT_VERSION (>= v$MIN_VERSION)"

# Extract session IDs matching the timestamp/hash format
SESSION_IDS=$(hermes sessions list | grep -oE '[0-9]{8}_[0-9]{6}_[a-f0-9]+' || true)

if [ -z "$SESSION_IDS" ]; then
  echo "No sessions found to delete."
  exit 0
fi

echo "----------------------------------------"
for id in $SESSION_IDS; do
  echo "Deleting session $id..."
  hermes sessions delete "$id" --yes
done

echo "----------------------------------------"
echo "All sessions cleared successfully."

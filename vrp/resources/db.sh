#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_FILE="$SCRIPT_DIR/docker-compose.yml"

docker compose -f "$COMPOSE_FILE" down

docker compose -f "$COMPOSE_FILE" up -d --renew-anon-volumes &&
  docker compose -f "$COMPOSE_FILE" logs -f

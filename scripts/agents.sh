#!/usr/bin/env bash
# agents — open multiple Herdr agents, one per new tab, in $PWD
set -euo pipefail

# ── config ──────────────────────────────────────────────────────────
MODE="${1:-}"
CWD="$(pwd -P)"
WORKSPACE_LABEL="$(basename "$CWD")"

# cook: one of each of these kinds
COOK_KINDS=(kilo opencode grok codex)

# ── helpers ─────────────────────────────────────────────────────────
need() { command -v "$1" >/dev/null || { echo "missing: $1" >&2; exit 1; }; }
need herdr
need jq

json_field() { jq -r "$1"; }

usage() {
  cat <<EOF
Usage:
  $(basename "$0") [COUNT] [KIND] [LABEL_PREFIX]
  $(basename "$0") cook

Examples:
  $(basename "$0")              # 3 × claude (default)
  $(basename "$0") 4 codex      # 4 × codex
  $(basename "$0") 2 grok worker
  $(basename "$0") cook         # 1× kilo, 1× opencode, 1× grok, 1× codex
EOF
  exit 1
}

# Ensure a Herdr server is reachable
if ! herdr status server >/dev/null 2>&1; then
  echo "No Herdr server running. Start one with: herdr" >&2
  exit 1
fi

# ── workspace (reuse by label, else create) ─────────────────────────
EXISTING=$(herdr workspace list 2>/dev/null | jq -r \
  --arg label "$WORKSPACE_LABEL" \
  '.result.workspaces[]? | select(.label == $label) | .workspace_id' | head -1 || true)

if [[ -n "${EXISTING:-}" ]]; then
  WS_ID="$EXISTING"
  echo "Using existing workspace: $WS_ID ($WORKSPACE_LABEL)"
else
  CREATED=$(herdr workspace create --cwd "$CWD" --label "$WORKSPACE_LABEL" --no-focus)
  WS_ID=$(printf '%s\n' "$CREATED" | json_field '.result.workspace.workspace_id')
  ROOT_PANE=$(printf '%s\n' "$CREATED" | json_field '.result.root_pane.pane_id')
  echo "Created workspace: $WS_ID  root pane: $ROOT_PANE"
fi

# ── resolve what to launch ──────────────────────────────────────────
declare -a KINDS=()
declare -a NAMES=()

if [[ "$MODE" == "cook" ]]; then
  for kind in "${COOK_KINDS[@]}"; do
    KINDS+=("$kind")
    NAMES+=("$kind")
  done
elif [[ "$MODE" == "-h" || "$MODE" == "--help" ]]; then
  usage
else
  COUNT="${1:-3}"
  KIND="${2:-claude}"
  LABEL_PREFIX="${3:-agent}"
  if ! [[ "$COUNT" =~ ^[1-9][0-9]*$ ]]; then
    echo "COUNT must be a positive integer" >&2
    usage
  fi
  for i in $(seq 1 "$COUNT"); do
    KINDS+=("$KIND")
    NAMES+=("${LABEL_PREFIX}${i}")
  done
fi

# ── create tabs + start agents ──────────────────────────────────────
for idx in "${!KINDS[@]}"; do
  KIND="${KINDS[$idx]}"
  NAME="${NAMES[$idx]}"
  TAB_LABEL="$NAME"

  TAB_JSON=$(herdr tab create --workspace "$WS_ID" --cwd "$CWD" --label "$TAB_LABEL" --no-focus)
  TAB_ID=$(printf '%s\n' "$TAB_JSON" | json_field '.result.tab.tab_id')
  PANE_ID=$(printf '%s\n' "$TAB_JSON" | json_field '.result.root_pane.pane_id')

  sleep 1.2

  echo "Starting $KIND as '$NAME' in tab $TAB_LABEL (pane $PANE_ID)…"
  herdr agent start "$NAME" --kind "$KIND" --pane "$PANE_ID" --timeout 45000 || {
    echo "  warning: agent start failed for $NAME (pane may still be usable)" >&2
  }
done

echo
echo "Done. ${#KINDS[@]} agent(s) opened in workspace '$WORKSPACE_LABEL'."
echo "Attach with:  herdr"
echo "List agents:  herdr agent list"

#!/usr/bin/env bash
# upload-wasm.sh
# Uploads ipfg.wasm to Cloudflare R2 via Wrangler.
# Run this whenever you rebuild the Wasm binary.
# Usage: ./upload-wasm.sh [--preview]

set -euo pipefail

WASM_PATH=".build-wasm/ipfg.wasm"
R2_BUCKET="ipfg-wasm"
R2_KEY="ipfg.wasm"

if [[ ! -f "$WASM_PATH" ]]; then
  echo "❌  $WASM_PATH not found. Build the Wasm first."
  exit 1
fi

SIZE=$(du -sh "$WASM_PATH" | cut -f1)
echo "📦  Uploading $WASM_PATH ($SIZE) → r2://$R2_BUCKET/$R2_KEY"

PREVIEW_FLAG=""
if [[ "${1:-}" == "--preview" ]]; then
  PREVIEW_FLAG="--preview"
  echo "⚠️   Using preview bucket"
fi

pnpm wrangler r2 object put "$R2_BUCKET/$R2_KEY" \
  --file "$WASM_PATH" \
  --content-type "application/wasm" \
  $PREVIEW_FLAG

echo "✅  Upload complete."
echo ""
echo "Next steps:"
echo "  pnpm wrangler deploy"

#!/usr/bin/env bash
# Authorization / role checks used during development.
# Requires smoke.sh to have run first (creates admin@example.com and rec@example.com).
set -euo pipefail
BASE="${BASE_URL:-http://127.0.0.1:8089}"

TOKEN=$(curl -s -X POST $BASE/api/auth/login -H 'content-type: application/json' \
  -d '{"email":"admin@example.com","password":"password123"}' \
  | python3 -c "import sys,json;print(json.load(sys.stdin)['token'])")

echo "== users (admin) =="
curl -s $BASE/api/users -H "authorization: Bearer $TOKEN" | python3 -m json.tool

echo "== second user tries to self-register as admin (expect 403) =="
curl -s -o /dev/null -w "%{http_code}\n" -X POST $BASE/api/auth/register -H 'content-type: application/json' \
  -d '{"email":"evil@example.com","password":"password123","name":"Eve","role":"admin"}'

echo "== recruiter cannot list users (expect 403) =="
RTOKEN=$(curl -s -X POST $BASE/api/auth/login -H 'content-type: application/json' \
  -d '{"email":"rec@example.com","password":"password123"}' \
  | python3 -c "import sys,json;print(json.load(sys.stdin)['token'])")
curl -s -o /dev/null -w "%{http_code}\n" $BASE/api/users -H "authorization: Bearer $RTOKEN"
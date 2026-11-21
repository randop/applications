#!/usr/bin/env bash
# Null-clearing (PATCH with explicit null) and delete/cascade checks used during
# development. Self-contained: creates its own job/candidate/application/note.
# Requires smoke.sh to have run first (needs admin@example.com).
set -euo pipefail
BASE="${BASE_URL:-http://127.0.0.1:8089}"
jid() { python3 -c "import sys,json;print(json.load(sys.stdin)['id'])"; }

TOKEN=$(curl -s -X POST $BASE/api/auth/login -H 'content-type: application/json' \
  -d '{"email":"admin@example.com","password":"password123"}' \
  | python3 -c "import sys,json;print(json.load(sys.stdin)['token'])")

JOB_ID=$(curl -s -X POST $BASE/api/jobs -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d '{"title":"Mutation Test","description":"tmp","location":"Here"}' | jid)

CAND_ID=$(curl -s -X POST $BASE/api/candidates -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d '{"first_name":"Mut","last_name":"Tester","email":"mut@example.com","phone":"555"}' | jid)

APP_ID=$(curl -s -X POST $BASE/api/applications -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d "{\"job_id\":\"$JOB_ID\",\"candidate_id\":\"$CAND_ID\"}" | jid)

NOTE_ID=$(curl -s -X POST $BASE/api/applications/$APP_ID/notes -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d '{"content":"note"}' | jid)

echo "== patch job location=null (clear) =="
curl -s -X PATCH $BASE/api/jobs/$JOB_ID -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' -d '{"location":null}' \
  | python3 -c "import sys,json;d=json.load(sys.stdin);print('location=',d['location'],'department=',d['department'])"

echo "== patch candidate phone=null (clear) =="
curl -s -X PATCH $BASE/api/candidates/$CAND_ID -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' -d '{"phone":null}' \
  | python3 -c "import sys,json;d=json.load(sys.stdin);print('phone=',d['phone'],'email=',d['email'])"

echo "== delete note, then list (expect []) =="
curl -s -o /dev/null -w "delete=%{http_code}\n" -X DELETE $BASE/api/notes/$NOTE_ID -H "authorization: Bearer $TOKEN"
curl -s $BASE/api/applications/$APP_ID/notes -H "authorization: Bearer $TOKEN"; echo

echo "== delete application, then get (expect 404) =="
curl -s -o /dev/null -w "delete=%{http_code}\n" -X DELETE $BASE/api/applications/$APP_ID -H "authorization: Bearer $TOKEN"
curl -s -o /dev/null -w "get=%{http_code}\n" $BASE/api/applications/$APP_ID -H "authorization: Bearer $TOKEN"

echo "== cleanup job + candidate =="
curl -s -o /dev/null -w "job_delete=%{http_code}\n" -X DELETE $BASE/api/jobs/$JOB_ID -H "authorization: Bearer $TOKEN"
curl -s -o /dev/null -w "candidate_delete=%{http_code}\n" -X DELETE $BASE/api/candidates/$CAND_ID -H "authorization: Bearer $TOKEN"
#!/usr/bin/env bash
# Main end-to-end smoke test used during development.
# Exercises the full ATS flow: auth, jobs, candidates, applications, interviews,
# notes, stats, plus expected error responses.
set -euo pipefail
BASE="${BASE_URL:-http://127.0.0.1:8089}"
jget() { python3 -c "import sys,json;print(json.load(sys.stdin).get('$1',''))"; }

echo "== health =="
curl -s $BASE/health; echo

echo "== register admin =="
REG=$(curl -s -X POST $BASE/api/auth/register -H 'content-type: application/json' \
  -d '{"email":"admin@example.com","password":"password123","name":"Ada Admin"}')
echo "$REG"
TOKEN=$(echo "$REG" | jget token)
echo "token: ${TOKEN:0:16}..."

echo "== register recruiter (second user) =="
curl -s -X POST $BASE/api/auth/register -H 'content-type: application/json' \
  -d '{"email":"rec@example.com","password":"password123","name":"Rob Recruiter"}' | jget role; echo

echo "== login =="
curl -s -X POST $BASE/api/auth/login -H 'content-type: application/json' \
  -d '{"email":"admin@example.com","password":"password123"}' | jget role; echo

echo "== me =="
curl -s $BASE/api/me -H "authorization: Bearer $TOKEN"; echo

echo "== create job =="
JOB=$(curl -s -X POST $BASE/api/jobs -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d '{"title":"Senior Rust Engineer","description":"Build APIs","department":"Engineering","location":"Remote","employment_type":"full_time"}')
echo "$JOB"
JOB_ID=$(echo "$JOB" | jget id)
echo "job_id: $JOB_ID"

echo "== list jobs =="
curl -s "$BASE/api/jobs?status=open&limit=10" -H "authorization: Bearer $TOKEN"; echo

echo "== create candidate =="
CAND=$(curl -s -X POST $BASE/api/candidates -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d '{"first_name":"Grace","last_name":"Hopper","email":"grace@example.com","phone":"555-0100","source":"referral"}')
echo "$CAND"
CAND_ID=$(echo "$CAND" | jget id)
echo "candidate_id: $CAND_ID"

echo "== search candidates =="
curl -s "$BASE/api/candidates?q=grace" -H "authorization: Bearer $TOKEN"; echo

echo "== create application =="
APP=$(curl -s -X POST $BASE/api/applications -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d "{\"job_id\":\"$JOB_ID\",\"candidate_id\":\"$CAND_ID\"}")
echo "$APP"
APP_ID=$(echo "$APP" | jget id)
echo "application_id: $APP_ID"

echo "== duplicate application (expect 409) =="
curl -s -o /dev/null -w "%{http_code}\n" -X POST $BASE/api/applications -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d "{\"job_id\":\"$JOB_ID\",\"candidate_id\":\"$CAND_ID\"}"

echo "== update application stage -> screening =="
curl -s -X PATCH $BASE/api/applications/$APP_ID -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d '{"stage":"screening"}'; echo

echo "== create interview =="
curl -s -X POST $BASE/api/interviews -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d "{\"application_id\":\"$APP_ID\",\"scheduled_at\":\"2026-12-01T15:00:00Z\",\"duration_minutes\":60,\"location\":\"Zoom\"}"; echo

echo "== create note =="
curl -s -X POST $BASE/api/applications/$APP_ID/notes -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d '{"content":"Strong systems background."}'; echo

echo "== list notes =="
curl -s $BASE/api/applications/$APP_ID/notes -H "authorization: Bearer $TOKEN"; echo

echo "== stats =="
curl -s $BASE/api/stats -H "authorization: Bearer $TOKEN"; echo

echo "== unauthorized (expect 401) =="
curl -s -o /dev/null -w "%{http_code}\n" $BASE/api/jobs

echo "== validation error (expect 400) =="
curl -s -o /dev/null -w "%{http_code}\n" -X POST $BASE/api/auth/register -H 'content-type: application/json' \
  -d '{"email":"x","password":"short","name":""}'

echo "== done =="
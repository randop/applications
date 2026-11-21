#!/usr/bin/env bash
set -euo pipefail
BASE="${BASE_URL:-http://127.0.0.1:8089}"
gql() { # $1 = query, $2 = variables(json), $3 = token(optional)
  local query="$1" vars="$2" tok="${3:-}"
  local data
  data=$(python3 -c 'import json,sys; print(json.dumps({"query": sys.argv[1], "variables": json.loads(sys.argv[2])}))' "$query" "$vars")
  if [ -n "$tok" ]; then
    curl -s -X POST "$BASE/graphql" -H 'content-type: application/json' -H "authorization: Bearer $tok" -d "$data"
  else
    curl -s -X POST "$BASE/graphql" -H 'content-type: application/json' -d "$data"
  fi
}
jq_py() { python3 -c "import sys,json;d=json.load(sys.stdin);print($1)"; }

echo "== register =="
REG=$(gql 'mutation($input: RegisterInput!) { register(input: $input) { token user { id email name role } } }' \
  '{"input":{"email":"admin@gql.com","password":"password123","name":"Gql Admin"}}')
echo "$REG"
TOKEN=$(echo "$REG" | jq_py "d['data']['register']['token']")

echo "== me =="
gql 'query { me { id email role } }' '{}' "$TOKEN"; echo

echo "== createJob =="
JOB=$(gql 'mutation($input: CreateJobInput!) { createJob(input: $input) { id title status employmentType } }' \
  '{"input":{"title":"GQL Engineer","description":"desc","employmentType":"FULL_TIME","status":"OPEN"}}' "$TOKEN")
echo "$JOB"
JOB_ID=$(echo "$JOB" | jq_py "d['data']['createJob']['id']")

echo "== jobs(status: OPEN) =="
gql 'query { jobs(status: OPEN) { id title status } }' '{}' "$TOKEN"; echo

echo "== createCandidate =="
CAND=$(gql 'mutation($input: CreateCandidateInput!) { createCandidate(input: $input) { id firstName lastName email } }' \
  '{"input":{"firstName":"Ada","lastName":"Lovelace","email":"ada@gql.com"}}' "$TOKEN")
echo "$CAND"
CAND_ID=$(echo "$CAND" | jq_py "d['data']['createCandidate']['id']")

echo "== createApplication =="
APP=$(gql 'mutation($input: CreateApplicationInput!) { createApplication(input: $input) { id jobId candidateId stage status } }' \
  "{\"input\":{\"jobId\":\"$JOB_ID\",\"candidateId\":\"$CAND_ID\"}}" "$TOKEN")
echo "$APP"
APP_ID=$(echo "$APP" | jq_py "d['data']['createApplication']['id']")

echo "== updateApplication(stage: SCREENING) =="
gql 'mutation($id: ID!, $input: UpdateApplicationInput!) { updateApplication(id: $id, input: $input) { id stage } }' \
  "{\"id\":\"$APP_ID\",\"input\":{\"stage\":\"SCREENING\"}}" "$TOKEN"; echo

echo "== createInterview =="
gql 'mutation($input: CreateInterviewInput!) { createInterview(input: $input) { id applicationId scheduledAt durationMinutes status } }' \
  "{\"input\":{\"applicationId\":\"$APP_ID\",\"scheduledAt\":\"2026-12-01T15:00:00Z\",\"durationMinutes\":60,\"location\":\"Zoom\"}}" "$TOKEN"; echo

echo "== createNote =="
NOTE=$(gql 'mutation($applicationId: ID!, $input: CreateNoteInput!) { createNote(applicationId: $applicationId, input: $input) { id content authorId } }' \
  "{\"applicationId\":\"$APP_ID\",\"input\":{\"content\":\"strong candidate\"}}" "$TOKEN")
echo "$NOTE"
NOTE_ID=$(echo "$NOTE" | jq_py "d['data']['createNote']['id']")

echo "== notes =="
gql 'query($applicationId: ID!) { notes(applicationId: $applicationId) { id content } }' \
  "{\"applicationId\":\"$APP_ID\"}" "$TOKEN"; echo

echo "== stats =="
gql 'query { stats { jobsTotal jobsOpen candidatesTotal applicationsTotal pipeline { stage count } } }' '{}' "$TOKEN"; echo

echo "== unauthorized jobs (expect errors) =="
gql 'query { jobs { id } }' '{}'; echo

echo "== deleteNote / deleteApplication =="
gql 'mutation($id: ID!) { deleteNote(id: $id) }' "{\"id\":\"$NOTE_ID\"}" "$TOKEN"; echo
gql 'mutation($id: ID!) { deleteApplication(id: $id) }' "{\"id\":\"$APP_ID\"}" "$TOKEN"; echo
echo "== done =="
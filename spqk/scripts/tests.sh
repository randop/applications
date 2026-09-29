#!/usr/bin/env bash
set -uo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SPQK_BIN="${SPQK_BIN:-$ROOT_DIR/build/spqk}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/spqk-tests.XXXXXX")"
STDOUT_FILE="$TMP_DIR/stdout"
STDERR_FILE="$TMP_DIR/stderr"
FAILURES=0
TESTS=0

cleanup() {
  rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT

pass() {
  printf 'PASS: %s\n' "$1"
  TESTS=$((TESTS + 1))
}

fail() {
  printf 'FAIL: %s\n' "$1"
  TESTS=$((TESTS + 1))
  FAILURES=$((FAILURES + 1))
}

run_success() {
  local name="$1"
  shift
  if "$SPQK_BIN" "$@" >"$STDOUT_FILE" 2>"$STDERR_FILE"; then
    return 0
  fi
  fail "$name (command returned nonzero)"
  return 1
}

expect_data_error() {
  local name="$1"
  local payload="$2"
  local expected="$3"
  if "$SPQK_BIN" --data "$payload" >"$STDOUT_FILE" 2>"$STDERR_FILE"; then
    fail "$name (unexpected success)"
  elif grep -Fq -- "$expected" "$STDERR_FILE"; then
    pass "$name"
  else
    fail "$name (missing expected diagnostic: $expected)"
  fi
}

contains() {
  grep -Fq -- "$2" "$1"
}

is_wav() {
  local path="$1"
  [[ -s "$path" ]] && [[ "$(dd if="$path" bs=1 count=4 2>/dev/null)" == "RIFF" ]]
}

if [[ ! -x "$SPQK_BIN" ]]; then
  fail "built executable exists ($SPQK_BIN)"
  printf 'Summary: %d tests, %d failures\n' "$TESTS" "$FAILURES"
  exit 1
fi

if run_success "--help succeeds" --help; then
  if contains "$STDOUT_FILE" "Usage: spqk [options]" && contains "$STDOUT_FILE" "--run-demo"; then
    pass "--help prints usage and options to stdout"
  else
    fail "--help prints usage and options to stdout"
  fi
  if grep -Eq '^spqk\.help status=ok code=0 next=stdout$' "$STDERR_FILE"; then
    pass "--help emits a structured stderr event"
  else
    fail "--help emits a structured stderr event"
  fi
fi

if run_success "--version succeeds" --version; then
  if grep -Eq '^spqk [^[:space:]]+$' "$STDOUT_FILE"; then
    pass "--version prints the version on stdout"
  else
    fail "--version prints the version on stdout"
  fi
  if grep -Eq '^spqk\.version status=ok code=0 version=[^[:space:]]+ next=stdout$' "$STDERR_FILE"; then
    pass "--version emits a structured stderr event"
  else
    fail "--version emits a structured stderr event"
  fi
  if ! grep -q 'spqk\.' "$STDOUT_FILE" && ! grep -q '^spqk ' "$STDERR_FILE"; then
    pass "version result and logs use separate streams"
  else
    fail "version result and logs use separate streams"
  fi
fi

if run_success "--list-voices succeeds" --list-voices; then
  voices_json="$(cat -- "$STDOUT_FILE")"
  if [[ "${voices_json:0:1}" == "[" && "${voices_json: -1}" == "]" ]]; then
    pass "--list-voices prints a JSON array on stdout"
  else
    fail "--list-voices prints a JSON array on stdout"
  fi
  if grep -Eq '^spqk\.voice\.list status=ok code=0 count=[0-9]+ next=stdout$' "$STDERR_FILE"; then
    pass "--list-voices emits a structured stderr event"
  else
    fail "--list-voices emits a structured stderr event"
  fi
fi

if run_success "--run-demo succeeds" --run-demo; then
  if contains "$STDERR_FILE" "spqk.demo status=start code=0" &&
     contains "$STDERR_FILE" "spqk.demo status=ok code=0"; then
    pass "--run-demo emits start and completion events"
  else
    fail "--run-demo emits start and completion events"
  fi
  if grep -Eq '^spqk\.synth\.(start|done) status=(start|ok) code=0 id=[0-9]+' "$STDERR_FILE"; then
    pass "demo synthesis events include stable id fields"
  else
    fail "demo synthesis events include stable id fields"
  fi
fi

SINGLE_WAV="$TMP_DIR/single.wav"
if run_success "single object payload succeeds" --data "{\"text\":\"Single item.\",\"voice\":\"en-us\",\"rate\":175,\"pitch\":50,\"volume\":100,\"output_file\":\"$SINGLE_WAV\"}"; then
  if is_wav "$SINGLE_WAV"; then pass "single object payload creates a WAV"; else fail "single object payload creates a WAV"; fi
  if grep -Eq '^spqk\.synth\.start status=start code=0 id=[0-9]+ voice=en-us output=wav next=initialize$' "$STDERR_FILE" &&
     grep -Eq '^spqk\.synth\.done status=ok code=0 id=[0-9]+ next=ready$' "$STDERR_FILE"; then
    pass "synthesis start/done log format is key=value"
  else
    fail "synthesis start/done log format is key=value"
  fi
fi

SHORT_WAV="$TMP_DIR/short.wav"
if run_success "-d alias succeeds" -d "{\"text\":\"Short option.\",\"output_file\":\"$SHORT_WAV\"}"; then
  if is_wav "$SHORT_WAV"; then pass "-d alias writes its output file"; else fail "-d alias writes its output file"; fi
fi

if run_success "playback payload succeeds" --data '{"text":"Playback route."}'; then
  if grep -Eq '^spqk\.synth\.start status=start code=0 id=[0-9]+ voice=default output=playback next=initialize$' "$STDERR_FILE"; then
    pass "payload without output_file selects playback"
  else
    fail "payload without output_file selects playback"
  fi
fi

ARRAY_FIRST="$TMP_DIR/array-first.wav"
ARRAY_SECOND="$TMP_DIR/array-second.wav"
if run_success "array payload succeeds" --data "[{\"text\":\"First item.\",\"output_file\":\"$ARRAY_FIRST\"},{\"text\":\"Second item.\",\"output_file\":\"$ARRAY_SECOND\"}]"; then
  if is_wav "$ARRAY_FIRST" && is_wav "$ARRAY_SECOND"; then
    pass "array items create separate WAV files"
  else
    fail "array items create separate WAV files"
  fi
fi

MERGE_OUTPUT="$ROOT_DIR/spqk-output.wav"
if run_success "array items are merged into a single WAV" --data '[{"text":"First merged item."},{"text":"Second merged item."}]'; then
  if is_wav "$MERGE_OUTPUT"; then
    pass "array without output_file merges into spqk-output.wav"
  else
    fail "array without output_file merges into spqk-output.wav"
  fi
  if grep -Eq '^spqk\.batch\.merge status=ok code=0 items=2 output=spqk-output\.wav next=exit$' "$STDERR_FILE"; then
    pass "array merge emits a structured batch.merge event"
  else
    fail "array merge emits a structured batch.merge event"
  fi
  rm -f "$MERGE_OUTPUT"
fi

BATCH_WAV="$TMP_DIR/batch.wav"
BATCH_OVERRIDE="$TMP_DIR/batch-override.wav"
if run_success "batch envelope succeeds" --data "{\"output_file\":\"$BATCH_WAV\",\"items\":[{\"text\":\"First batch item.\"},{\"text\":\"Second batch item.\",\"output_file\":\"$BATCH_OVERRIDE\"}]}"; then
  if is_wav "$BATCH_WAV" && is_wav "$BATCH_OVERRIDE"; then
    pass "batch envelope writes combined and per-item WAV files"
  else
    fail "batch envelope writes combined and per-item WAV files"
  fi
fi

VOICE_CRITERIA_WAV="$TMP_DIR/voice-criteria.wav"
if run_success "voice criteria payload succeeds" --data "{\"text\":\"Voice criteria.\",\"voice\":{\"language\":\"en-us\",\"gender\":\"female\",\"age\":25,\"variant\":0},\"output_file\":\"$VOICE_CRITERIA_WAV\"}"; then
  if is_wav "$VOICE_CRITERIA_WAV"; then pass "voice criteria object creates a WAV"; else fail "voice criteria object creates a WAV"; fi
fi

PARAMS_WAV="$TMP_DIR/parameters.wav"
if run_success "extended parameters payload succeeds" --data "{\"text\":\"Parameter settings.\",\"range\":50,\"punctuation\":\"some\",\"punctuation_list\":\"!?.,\",\"capitals\":1,\"word_gap\":10,\"intonation\":0,\"ssml_break_mul\":100,\"output_file\":\"$PARAMS_WAV\"}"; then
  if is_wav "$PARAMS_WAV"; then pass "extended speech parameters are accepted"; else fail "extended speech parameters are accepted"; fi
fi

expect_data_error "malformed JSON is rejected" "{" "invalid --data payload:"
expect_data_error "non-object top-level JSON is rejected" "42" "JSON payload must be an object or an array"
expect_data_error "missing text is rejected" '{"voice":"en-us"}' "field 'text' is required"
expect_data_error "empty text is rejected" '{"text":""}' "field 'text' is required"
expect_data_error "wrong text type is rejected" '{"text":42}' "field 'text' is required"
expect_data_error "unknown payload fields are rejected" '{"text":"x","mystery":1}' "unknown field 'mystery'"
expect_data_error "unknown voice criteria are rejected" '{"text":"x","voice":{"language":"en","accent":"west"}}' "unknown voice field 'accent'"
expect_data_error "empty arrays are rejected" '[]' "speech item array must not be empty"
expect_data_error "non-object array entries are rejected" '[{"text":"x"},7]' "each speech item must be a JSON object"
expect_data_error "invalid batch envelope is rejected" '{"items":{},"output_file":"x.wav"}' "'items' must be a non-empty array"
expect_data_error "unknown batch fields are rejected" '{"items":[{"text":"x"}],"extra":true}' "unknown batch field 'extra'"
expect_data_error "rate below range is rejected" '{"text":"x","rate":79}' "field 'rate' must be between 80 and 450"
expect_data_error "rate above range is rejected" '{"text":"x","rate":451}' "field 'rate' must be between 80 and 450"
expect_data_error "pitch above range is rejected" '{"text":"x","pitch":101}' "field 'pitch' must be between 0 and 100"
expect_data_error "range below allowed minimum is rejected" '{"text":"x","range":-1}' "field 'range' must be between 0 and 100"
expect_data_error "invalid punctuation value is rejected" '{"text":"x","punctuation":"loud"}' "field 'punctuation' must be none, some, or all"
expect_data_error "wrong punctuation-list type is rejected" '{"text":"x","punctuation_list":4}' "field 'punctuation_list' must be a string"
expect_data_error "wrong voice type is rejected" '{"text":"x","voice":false}' "field 'voice' must be a string or voice criteria object"
expect_data_error "invalid voice gender is rejected" '{"text":"x","voice":{"gender":"neutral-ish"}}' "field 'gender' must be male, female, or unspecified"
expect_data_error "voice age above range is rejected" '{"text":"x","voice":{"age":256}}' "voice field 'age' is out of range"

if "$SPQK_BIN" --data "{\"text\":\"x\",\"voice\":\"__spqk_missing_voice__\",\"output_file\":\"$TMP_DIR/missing.wav\"}" >"$STDOUT_FILE" 2>"$STDERR_FILE"; then
  fail "unknown voice selection is rejected"
else
  if grep -Eq '^spqk\.synth\.error status=error code=[0-9-]+ id=[0-9]+ reason=voice_selection_failed next=exit$' "$STDERR_FILE"; then
    pass "unknown voice selection logs a structured synthesis error"
  else
    fail "unknown voice selection logs a structured synthesis error"
  fi
fi

if "$SPQK_BIN" --data >"$STDOUT_FILE" 2>"$STDERR_FILE"; then
  fail "--data without a value is rejected"
elif grep -Eiq 'argument.*(missing|required)|requires.*argument|missing.*argument' "$STDERR_FILE"; then
  pass "--data without a value is rejected"
else
  fail "--data without a value is rejected"
fi

"$SPQK_BIN" --help >"$STDOUT_FILE" 2>"$STDERR_FILE"
if grep -q -- '--output' "$STDOUT_FILE"; then
  if run_success "--output=json succeeds" --output=json --version; then
    if grep -q 'application/json\|"version"' "$STDOUT_FILE"; then pass "--output=json result is JSON"; else fail "--output=json result is JSON"; fi
  fi
else
  pass "--output=json mode (not implemented; optional case skipped)"
fi

printf 'Summary: %d tests, %d failures\n' "$TESTS" "$FAILURES"
if (( FAILURES > 0 )); then exit 1; fi
exit 0

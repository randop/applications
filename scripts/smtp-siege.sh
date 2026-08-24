#!/usr/bin/env bash
#
# smtp-siege.sh — concurrent SMTP load tool built on swaks.
#
# Fires N swaks transactions at a target SMTP server with a configurable
# concurrency level, then reports throughput, error breakdown, and
# latency percentiles (min/p50/p90/p95/p99/max).
#
# Requires: swaks, awk, sort, bash 4.3+ (uses `wait -n`).

set -euo pipefail

# ---- defaults -----------------------------------------------------------
HOST=""
PORT=25
CONCURRENCY=10
COUNT=100
DURATION=0            # if >0, run for this many seconds instead of COUNT
FROM="siege@example.com"
TO="test@example.com"
TLS_MODE=""           # "", "tls", "tls-optional", "tls-on-connect"
AUTH_TYPE=""
AUTH_USER=""
AUTH_PASS=""
BODY_SIZE=1024         # bytes of random body payload
TIMEOUT=10             # per-connection swaks timeout, seconds
OUTDIR=""
KEEP_OUTDIR=0

usage() {
    cat <<'EOF'
Usage: smtp-siege.sh -H host [options]

Required:
  -H, --host HOST          Target SMTP server hostname/IP

Load shape (pick one; -n is default):
  -n, --count N             Total messages to send            (default: 100)
  -D, --duration SECS       Run for this many seconds instead of a fixed count

Connection:
  -P, --port PORT           SMTP port                          (default: 25)
  -c, --concurrency N       Parallel workers                   (default: 10)
  -T, --timeout SECS        Per-transaction timeout             (default: 10)

Message:
  -f, --from ADDR           MAIL FROM                (default: siege@example.com)
  -t, --to ADDR             RCPT TO                   (default: test@example.com)
  -b, --body-size BYTES     Random body payload size in bytes  (default: 1024)

TLS:
  --tls                     Require STARTTLS, abort txn if unavailable
  --tls-optional             Attempt STARTTLS, fall back to plaintext
  --tls-on-connect           Implicit TLS on connect (SMTPS, default port 465)

Auth (all three required together if used):
  --auth-type TYPE           e.g. PLAIN, LOGIN, CRAM-MD5
  --auth-user USER
  --auth-pass PASS

Output:
  -o, --outdir DIR           Keep per-request logs + raw results here
                              (default: temp dir, deleted on exit)
  -h, --help                  Show this help

Examples:
  # 500 messages, 50 concurrent, plaintext
  ./smtp-siege.sh -H mail.example.com -n 500 -c 50

  # 30-second STARTTLS siege at 20 concurrent workers
  ./smtp-siege.sh -H mail.example.com --tls -D 30 -c 20

  # Authenticated siege, keep raw logs for inspection
  ./smtp-siege.sh -H mail.example.com -c 25 -n 1000 \
      --auth-type LOGIN --auth-user bench --auth-pass secret \
      -o ./siege-results
EOF
}

# ---- arg parsing ----------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        -H|--host) HOST="$2"; shift 2 ;;
        -P|--port) PORT="$2"; shift 2 ;;
        -c|--concurrency) CONCURRENCY="$2"; shift 2 ;;
        -n|--count) COUNT="$2"; shift 2 ;;
        -D|--duration) DURATION="$2"; shift 2 ;;
        -f|--from) FROM="$2"; shift 2 ;;
        -t|--to) TO="$2"; shift 2 ;;
        -b|--body-size) BODY_SIZE="$2"; shift 2 ;;
        -T|--timeout) TIMEOUT="$2"; shift 2 ;;
        --tls) TLS_MODE="tls"; shift ;;
        --tls-optional) TLS_MODE="tls-optional"; shift ;;
        --tls-on-connect) TLS_MODE="tls-on-connect"; shift ;;
        --auth-type) AUTH_TYPE="$2"; shift 2 ;;
        --auth-user) AUTH_USER="$2"; shift 2 ;;
        --auth-pass) AUTH_PASS="$2"; shift 2 ;;
        -o|--outdir) OUTDIR="$2"; KEEP_OUTDIR=1; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

if [[ -z "$HOST" ]]; then
    echo "error: -H/--host is required" >&2
    usage
    exit 1
fi

if ! command -v swaks >/dev/null 2>&1; then
    echo "error: swaks not found on PATH" >&2
    exit 1
fi

if [[ -z "$OUTDIR" ]]; then
    OUTDIR="$(mktemp -d /tmp/smtp-siege.XXXXXX)"
fi
mkdir -p "$OUTDIR"
RESULTS="$OUTDIR/results.csv"
: > "$RESULTS"

cleanup() {
    if [[ "$KEEP_OUTDIR" -eq 0 ]]; then
        rm -rf "$OUTDIR"
    fi
}
trap cleanup EXIT

# ---- build the swaks base command -----------------------------------------
BASE_ARGS=(--server "${HOST}:${PORT}" --from "$FROM" --to "$TO"
           --timeout "$TIMEOUT" --silent 2 --suppress-data)

case "$TLS_MODE" in
    tls) BASE_ARGS+=(--tls) ;;
    tls-optional) BASE_ARGS+=(--tls-optional) ;;
    tls-on-connect) BASE_ARGS+=(--tls-on-connect) ;;
esac

if [[ -n "$AUTH_TYPE" ]]; then
    BASE_ARGS+=(--auth "$AUTH_TYPE" --auth-user "$AUTH_USER" --auth-password "$AUTH_PASS")
fi

# Random body of BODY_SIZE bytes, base64'd so it's safe SMTP body content.
BODY_FILE="$OUTDIR/body.txt"
head -c "$BODY_SIZE" /dev/urandom | base64 > "$BODY_FILE"
BASE_ARGS+=(--body "@${BODY_FILE}")

# ---- exit code -> human label (see `perldoc swaks`, EXIT CODES section) ---
exit_label() {
    case "$1" in
        0) echo "ok" ;;
        1) echo "bad_args" ;;
        2) echo "connect_error" ;;
        6) echo "conn_closed" ;;
        21) echo "banner_error" ;;
        22) echo "helo_error" ;;
        23) echo "mail_from_error" ;;
        24) echo "rcpt_rejected" ;;
        25) echo "data_rejected" ;;
        26) echo "post_data_rejected" ;;
        27) echo "quit_error" ;;
        28) echo "auth_error" ;;
        29) echo "tls_error" ;;
        124) echo "timeout" ;;
        *) echo "error_${1}" ;;
    esac
}

# ---- one worker transaction ------------------------------------------------
# Runs as a bash background job (&) in the *same* shell as the dispatcher, so
# it inherits BASE_ARGS/OUTDIR/RESULTS/TIMEOUT directly — no export/eval
# gymnastics needed, unlike a plain `xargs -P ... bash -c` fan-out.
run_one() {
    local id="$1"
    local start end elapsed_ms rc label
    start=$(date +%s%N)
    if timeout "$((TIMEOUT + 5))" swaks "${BASE_ARGS[@]}" \
            > "$OUTDIR/req_${id}.log" 2>&1; then
        rc=0
    else
        rc=$?
    fi
    end=$(date +%s%N)
    elapsed_ms=$(( (end - start) / 1000000 ))
    label=$(exit_label "$rc")
    echo "${id},${rc},${label},${elapsed_ms}" >> "$RESULTS"
}

# ---- dispatcher -------------------------------------------------------
# Admits at most CONCURRENCY in-flight jobs at a time, gated on actual
# completions via `wait -n` (bash 4.3+). This is what makes duration mode
# correct: unlike feeding an unbounded ID stream into `xargs -P`, admission
# here can never outrun the workers, so it can't build a backlog that
# overshoots the requested wall-clock window.
run_all() {
    local next_id=1
    while true; do
        if [[ "$DURATION" -gt 0 ]]; then
            [[ "$(date +%s)" -ge "$STOP_AT" ]] && break
        else
            [[ "$next_id" -gt "$COUNT" ]] && break
        fi

        while [[ "$(jobs -rp | wc -l)" -ge "$CONCURRENCY" ]]; do
            wait -n || true
        done

        run_one "$next_id" &
        next_id=$((next_id + 1))
    done
    wait
}

echo "Target:      ${HOST}:${PORT}"
[[ -n "$TLS_MODE" ]] && echo "TLS mode:     $TLS_MODE"
[[ -n "$AUTH_TYPE" ]] && echo "Auth:         $AUTH_TYPE"
echo "Concurrency:  $CONCURRENCY"
if [[ "$DURATION" -gt 0 ]]; then
    echo "Duration:     ${DURATION}s"
else
    echo "Count:        $COUNT"
fi
echo "Body size:    ${BODY_SIZE} bytes"
echo "Results dir:  $OUTDIR"
echo

SIEGE_START=$(date +%s%N)

if [[ "$DURATION" -gt 0 ]]; then
    STOP_AT=$(( $(date +%s) + DURATION ))
fi
run_all

SIEGE_END=$(date +%s%N)
TOTAL_WALL_MS=$(( (SIEGE_END - SIEGE_START) / 1000000 ))

# ---- report -----------------------------------------------------------
TOTAL=$(wc -l < "$RESULTS" | tr -d ' ')
OK=$(awk -F, '$2==0' "$RESULTS" | wc -l | tr -d ' ')
FAIL=$(( TOTAL - OK ))

echo
echo "=== Results ==="
echo "Total requests:  $TOTAL"
echo "Succeeded:        $OK"
echo "Failed:            $FAIL"
if [[ "$TOTAL_WALL_MS" -gt 0 ]]; then
    RPS=$(awk -v t="$TOTAL" -v ms="$TOTAL_WALL_MS" 'BEGIN{printf "%.2f", t/(ms/1000)}')
    echo "Wall time:        $(awk -v ms="$TOTAL_WALL_MS" 'BEGIN{printf "%.2fs", ms/1000}')"
    echo "Throughput:       ${RPS} req/s"
fi

if [[ "$FAIL" -gt 0 ]]; then
    echo
    echo "Failure breakdown:"
    awk -F, '$2!=0 {print $3}' "$RESULTS" | sort | uniq -c | sort -rn | \
        awk '{printf "  %-20s %d\n", $2, $1}'
fi

if [[ "$OK" -gt 0 ]]; then
    echo
    echo "Latency (ms), successful requests only:"
    awk -F, '$2==0 {print $4}' "$RESULTS" | sort -n > "$OUTDIR/latencies.txt"

    n=$(wc -l < "$OUTDIR/latencies.txt" | tr -d ' ')
    pidx() { # nearest-rank percentile index for percentile $1 out of n
        awk -v n="$n" -v p="$1" 'BEGIN{i=int((p/100)*n); if(i<1)i=1; if(i>n)i=n; print i}'
    }
    getline_n() { sed -n "${1}p" "$OUTDIR/latencies.txt"; }

    MIN=$(getline_n 1)
    MAX=$(getline_n "$n")
    P50=$(getline_n "$(pidx 50)")
    P90=$(getline_n "$(pidx 90)")
    P95=$(getline_n "$(pidx 95)")
    P99=$(getline_n "$(pidx 99)")
    AVG=$(awk '{s+=$1} END{printf "%.1f", s/NR}' "$OUTDIR/latencies.txt")

    printf "  min: %sms  avg: %sms  p50: %sms  p90: %sms  p95: %sms  p99: %sms  max: %sms\n" \
        "$MIN" "$AVG" "$P50" "$P90" "$P95" "$P99" "$MAX"
fi

echo
echo "Raw per-request results: $RESULTS"
if [[ "$KEEP_OUTDIR" -eq 1 ]]; then
    echo "Per-request swaks transcripts kept in: $OUTDIR"
else
    echo "(pass -o/--outdir to keep raw logs and transcripts for inspection)"
fi

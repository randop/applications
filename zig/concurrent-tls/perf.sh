#!/usr/bin/env bash

set -euo pipefail

for i in $(seq 1 100); do
  (
    echo "hello from connection $i"
    sleep 5
  ) |
    timeout 10 openssl s_client -connect localhost:8443 -CAfile server.crt -quiet -verify_return_error -noservername 2>/dev/null &
done
wait

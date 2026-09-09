#!/usr/bin/env bash

curl -sv --tlsv1.3 --tls-max 1.3 https://slack.com/api/api.test -o /dev/null 2>&1 | grep -i "SSL connection using"

# example output:
# * SSL connection using TLSv1.3 / TLS_AES_256_GCM_SHA384 / x25519 / RSASSA-PSS

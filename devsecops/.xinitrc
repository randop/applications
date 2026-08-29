#!/bin/sh

# Disable the X screensaver blanking
xset s off
xset s noblank

# Disable DPMS (monitor power off/standby/suspend)
xset -dpms

# Waits for a set of files to appear, polling with an exponential
wait_for_file() {
    file="$1"
    attempt=0
 
    while [ ! -e "$file" ]; do
        sleep 1
        attempt=$((attempt + 1))
    done
 
    echo "Found: $file"
}

wait_for_file /tmp/log.txt
wait_for_file /tmp/svc.txt

exec /usr/local/bin/devsecops

#!/bin/sh

# Check if the log file exists
if [ ! -f /var/log/service.log ]; then
    echo "Log file /var/log/service.log does not exist"
    exit 1
fi

#!/bin/sh
#####################################################################
# POSIX complaint helper functions
#####################################################################

calc_cores() {
    local mem_gb=$(( $(awk '/MemTotal/ {print $2}' /proc/meminfo) / 1024 / 1024 ))
    local cpu_cores=$(nproc)
    local usable=$(( mem_gb < cpu_cores ? mem_gb : cpu_cores ))
    usable=$(( usable - 1 ))
    [ "$usable" -lt 1 ] && usable=1
    echo "$usable"
}

N=$(calc_cores)
echo "Using $N cores"

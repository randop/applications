#!/bin/sh

BUILD_FILE="build.number"

# Read build number from file
if [ -f "$BUILD_FILE" ]; then
    BUILD_NUM=$(cat "$BUILD_FILE")
else
    BUILD_NUM=0
fi

# Increment build number
BUILD_NUM=$((BUILD_NUM + 1))

# Write new build number to file
echo "$BUILD_NUM" > "$BUILD_FILE"

echo "Updated build number to $BUILD_NUM"
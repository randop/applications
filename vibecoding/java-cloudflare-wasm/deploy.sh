#!/bin/sh
set -eu

./build.sh
cd worker

if [ ! -d node_modules ]; then
    npm install
fi

npx wrangler deploy

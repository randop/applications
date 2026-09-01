#!/bin/sh
set -eu

mvn clean package

rm -f worker/app.wasm worker/app.wasm-runtime.js
cp target/worker/app.wasm worker/app.wasm
cp target/worker/app.wasm-runtime.js worker/app.wasm-runtime.js

echo "Build complete:"
ls -lh worker/app.wasm worker/app.wasm-runtime.js

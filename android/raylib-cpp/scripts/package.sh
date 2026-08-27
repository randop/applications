#!/usr/bin/env bash

set -euo pipefail

if [ ! -d .build ]; then
  meson setup --cross-file cross/android-arm64.txt .build
fi

rm -vf .build/libmain.so
meson compile -C .build

mkdir -pv lib/arm64-v8a
rm -fv lib/arm64-v8a/libmain.so
cp -v .build/libmain.so lib/arm64-v8a/
rm -fv unsigned.apk aligned.apk final.apk

aapt package -f -M android/AndroidManifest.xml \
  -S android/res -A android/assets \
  -I $ANDROID_SDK/platforms/android-34/android.jar \
  -F unsigned.apk

# TODO: add more architecture native libs
aapt add unsigned.apk lib/arm64-v8a/libmain.so

zipalign -f 4 unsigned.apk aligned.apk
apksigner sign \
  --ks android.keystore \
  --ks-key-alias android \
  --ks-pass pass:android --key-pass pass:android \
  --out final.apk \
  aligned.apk

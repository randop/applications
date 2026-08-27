#!/usr/bin/env bash

set -euo pipefail

if [ ! -f "subprojects/raygui/src/raygui.h" ]; then
  mkdir -p subprojects/raygui
  meson subprojects download raygui
  set +e
  meson subprojects update --reset raygui
  set -e
  cat <<EOF >subprojects/raygui/meson.build
project('raygui', 'c')
raygui_inc = include_directories('src', is_system: true)
raygui_dep = declare_dependency(
  include_directories: raygui_inc
)
meson.override_dependency('raygui', raygui_dep)
EOF
fi

if [ ! -d .build ]; then
  meson setup --cross-file cross/android-arm64.txt .build
fi

# sync version
sed -i "s/android:versionName=\"[^\"]*\"/android:versionName=\"$(cat VERSION)\"/" android/AndroidManifest.xml

# -------------------------------------------------------
# Build native library
# -------------------------------------------------------
rm -f .build/libmain.so
meson compile -C .build

# -------------------------------------------------------
# Compile Java
# -------------------------------------------------------
rm -rf .build/classes
mkdir -p .build/classes

javac -source 1.8 -target 1.8 \
  -Xlint:-options \
  -bootclasspath "$ANDROID_PLATFORM/android.jar" \
  -classpath "$ANDROID_PLATFORM/android.jar" \
  -d .build/classes \
  android/app/src/main/java/com/example/raylibcpp/NativeLoader.java

# -------------------------------------------------------
# Convert to classes.dex
# -------------------------------------------------------
rm -rf .build/dex
mkdir -p .build/dex

d8 --min-api 24 \
  --lib "$ANDROID_PLATFORM/android.jar" \
  --output .build/dex \
  $(find .build/classes -name "*.class")

# -------------------------------------------------------
# Prepare native library
# -------------------------------------------------------
rm -rf lib
mkdir -p lib/arm64-v8a
cp -v .build/libmain.so lib/arm64-v8a/

# -------------------------------------------------------
# Create base APK
# -------------------------------------------------------
rm -f unsigned.apk aligned.apk final.apk

aapt package -f \
  -M android/AndroidManifest.xml \
  -S android/res \
  -A android/assets \
  -I "$ANDROID_PLATFORM/android.jar" \
  -F unsigned.apk

# -------------------------------------------------------
# Add native library and classes.dex
# -------------------------------------------------------
aapt add unsigned.apk lib/arm64-v8a/libmain.so

cp .build/dex/classes.dex .
aapt add unsigned.apk classes.dex
rm -f classes.dex

# -------------------------------------------------------
# Align and Sign
# -------------------------------------------------------
zipalign -f 4 unsigned.apk aligned.apk

apksigner sign \
  --ks android.keystore \
  --ks-key-alias android \
  --ks-pass pass:android \
  --key-pass pass:android \
  --out final.apk \
  aligned.apk

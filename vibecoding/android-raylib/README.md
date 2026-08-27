# Raylib on Android

**Pure C++23 + raylib on Android via NativeActivity (no Java/Kotlin sources) + Meson is feasible.** Android’s `android.app.NativeActivity` (framework class) loads a single native shared library; set `android:hasCode="false"` and the `android.app.lib_name` meta-data. Raylib already implements the Android backend with `native_app_glue` and exposes `android_main` that calls your `main()`.

### Prerequisites
- Android NDK (r26+ recommended; r28c or newer works well).
- Android SDK (platform + build-tools matching your target API, e.g. 34/35).
- Meson ≥ 1.2 + Ninja.
- OpenJDK (only for `aapt`/`aapt2`, `apksigner`, `zipalign` – not for compiling your code).
- Optional: `adb` for install/debug.

Set environment variables (adjust paths):
```bash
export ANDROID_NDK=/path/to/ndk
export ANDROID_SDK=/path/to/sdk
export ANDROID_HOME=$ANDROID_SDK
export PATH="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin:$ANDROID_SDK/build-tools/35.0.0:$PATH"
```
(Use the correct host tag: `linux-x86_64`, `darwin-x86_64`, or `windows-x86_64`.)

Target API ≥ 24 (preferably 29+). Recommended ABIs: `arm64-v8a` (primary) + `armeabi-v7a` if needed. x86/x86_64 mainly for emulators.

### Project Layout (barebones)
```
raylib-android-cpp/
├── meson.build
├── meson_options.txt          # optional
├── cross/
│   ├── android-arm64.txt
│   └── android-armv7a.txt     # add more ABIs as needed
├── src/
│   └── main.cpp               # your C++23 code
├── android/
│   ├── AndroidManifest.xml
│   ├── res/                   # minimal icons/strings if desired
│   └── assets/                # optional game assets
└── scripts/
    └── package_apk.sh         # packaging helper
```

### 1. Minimal `main.cpp` (C++23 + raylib)
```cpp
#include "raylib.h"

int main() {
    // On Android the window size is usually ignored / set by the system
    InitWindow(0, 0, "Barebones C++23 raylib");
    SetTargetFPS(60);
    SetExitKey(0);  // prevent back button from immediately closing

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("Hello from pure C++23 + raylib on Android", 40, 40, 20, DARKGRAY);
        DrawFPS(10, 10);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
```
Raylib’s Android platform code supplies `android_main` and calls your `main()`. You do **not** write `ANativeActivity_onCreate` yourself when using raylib.

### 2. Meson cross files
Example `cross/android-arm64.txt` (API 34):
```ini
[constants]
ndk = '/path/to/ndk'          # or use environment
api = '34'
triple = 'aarch64-linux-android'
toolchain = ndk / 'toolchains/llvm/prebuilt/linux-x86_64'

[binaries]
c = toolchain / 'bin' / (triple + api + '-clang')
cpp = toolchain / 'bin' / (triple + api + '-clang++')
ar = toolchain / 'bin/llvm-ar'
strip = toolchain / 'bin/llvm-strip'
# pkg-config = ... (usually not needed)

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'

[properties]
# Prefer static C++ runtime to avoid shipping libc++_shared.so
# (or switch to c++_shared and package the .so)
cpp_link_args = ['-static-libstdc++']   # or rely on -stdlib=libc++ + static
sys_root = toolchain / 'sysroot'
```

For `armeabi-v7a` change the triple to `armv7a-linux-androideabi` and `cpu_family`/`cpu` accordingly. You can parameterize the API level and NDK path.

### 3. Root `meson.build`
```meson
project('raylib-android-cpp', 'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++23',
    'warning_level=2',
    'buildtype=release',
    'b_lto=true',          # optional size win
  ])

# --- raylib as CMake subproject (static) ---
cmake = import('cmake')
raylib_opts = cmake.subproject_options()
raylib_opts.add_cmake_defines({
  'PLATFORM': 'Android',
  'BUILD_EXAMPLES': 'OFF',
  'BUILD_SHARED_LIBS': 'OFF',
  'CMAKE_BUILD_TYPE': 'MinSizeRel',
  # Pass NDK toolchain if needed; Meson + CMake subproject usually picks up the cross environment
})
raylib_proj = cmake.subproject('raylib', options: raylib_opts)
raylib_dep = raylib_proj.dependency('raylib')

# native_app_glue is required
ndk = meson.get_external_property('ndk', get_option('android_ndk'))  # or hard-code
native_app_glue = files(ndk / 'sources/android/native_app_glue/android_native_app_glue.c')

inc = include_directories('src')

# Your shared library (the one NativeActivity loads)
lib = shared_library('main',           # → libmain.so ; match meta-data
  sources: ['src/main.cpp', native_app_glue],
  dependencies: [raylib_dep],
  include_directories: [
    inc,
    include_directories(ndk / 'sources/android/native_app_glue'),
  ],
  cpp_args: [
    '-DANDROID',
    '-DPLATFORM_ANDROID',
    '-D__ANDROID_API__=34',   # match your API
  ],
  link_args: [
    '-u', 'ANativeActivity_onCreate',   # force export of the entry point
    '-Wl,--build-id',
    '-Wl,-z,noexecstack',
    '-Wl,-z,relro',
    '-Wl,-z,now',
    '-landroid',
    '-llog',
    '-lEGL',
    '-lGLESv2',
    '-lOpenSLES',
    '-latomic',
    '-lm',
    '-ldl',
  ],
  name_prefix: 'lib',
  name_suffix: 'so',
  install: false,
)

# Optional: custom target that runs the packaging script after the .so is built
```

Place a `subprojects/raylib.wrap` (or clone raylib next to the project and point the CMake subproject at it). Raylib’s CMake supports `-DPLATFORM=Android` + the NDK toolchain file.

Alternative (often cleaner for control): build raylib once with its own Makefile/CMake for each ABI into `lib/<abi>/libraylib.a` and use `declare_dependency` + `find_library` instead of a live subproject.

### 4. AndroidManifest.xml (no custom Java)
```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.example.raylibcpp"
    android:versionCode="1"
    android:versionName="0.1.0">

    <uses-sdk android:minSdkVersion="24" android:targetSdkVersion="34"/>
    <uses-feature android:glEsVersion="0x00020000" android:required="true"/>

    <application
        android:label="Raylib C++23"
        android:hasCode="false"
        android:allowBackup="false"
        android:theme="@android:style/Theme.NoTitleBar.Fullscreen">

        <activity
            android:name="android.app.NativeActivity"
            android:exported="true"
            android:configChanges="orientation|keyboardHidden|screenSize"
            android:screenOrientation="landscape"
            android:launchMode="singleTask">

            <meta-data
                android:name="android.app.lib_name"
                android:value="main"/>   <!-- matches libmain.so -->

            <intent-filter>
                <action android:name="android.intent.action.MAIN"/>
                <category android:name="android.intent.category.LAUNCHER"/>
            </intent-filter>
        </activity>
    </application>
</manifest>
```

### 5. Packaging the APK (script)
A typical flow (adapt paths/ABIs):

1. Build the `.so` for each ABI with Meson (`meson setup --cross-file cross/android-arm64.txt build-arm64 && meson compile -C build-arm64`).
2. Copy `libmain.so` → `android/lib/arm64-v8a/libmain.so` (and other ABIs).
3. If you used the shared C++ runtime, also copy `libc++_shared.so` from the NDK.
4. Use `aapt` (or `aapt2`) to package:
   ```bash
   aapt package -f -M android/AndroidManifest.xml \
        -S android/res -A android/assets \
        -I $ANDROID_SDK/platforms/android-34/android.jar \
        -F unsigned.apk
   # add the native libs
   aapt add unsigned.apk lib/arm64-v8a/libmain.so
   # (repeat for other ABIs)
   ```
5. Align + sign:
   ```bash
   zipalign -f 4 unsigned.apk aligned.apk
   apksigner sign --ks your.keystore --out final.apk aligned.apk
   ```

You can wrap the above in a Meson `custom_target` or a simple shell script invoked after `meson compile`. For multi-ABI you usually produce one fat APK or use Android App Bundle later.

### Important Notes & Gotchas
- **Static vs shared raylib**: Prefer static (`libraylib.a`) to avoid the circular `main` / `android_main` dependency that appears when both are shared libraries.
- **C++ runtime**: `-static-libstdc++` (or NDK’s `c++_static`) keeps the APK smaller and simpler. If you need the shared runtime, ship `libc++_shared.so` for every ABI.
- **Entry point**: The `-u ANativeActivity_onCreate` linker flag is essential; raylib + native_app_glue provide the real implementation.
- **Assets**: Place them under `android/assets/`; raylib’s `Load*` functions look relative to the Android asset manager automatically on PLATFORM_ANDROID.
- **Config changes / lifecycle**: Raylib’s Android backend has historically been imperfect with activity recreation. Declaring `android:configChanges` helps; for production you may need to handle `destroyRequested` more carefully (see raylib issues).
- **Size**: Use `MinSizeRel`, LTO, and strip the `.so`. A minimal APK can be well under 1 MB.
- **Debugging**: `adb logcat | grep raylib` (or your tag). Use `adb install -r final.apk`.
- **Multiple ABIs**: Build once per cross-file, then assemble a single APK that contains `lib/<abi>/libmain.so` for each.

### Quick Start Commands
```bash
# one ABI example
meson setup --cross-file cross/android-arm64.txt build-arm64
meson compile -C build-arm64
# then run your package_apk.sh that copies the .so and runs aapt/apksigner
```

This gives a completely Java/Kotlin-free binary package driven by Meson + NDK toolchain. For a more polished multi-ABI / asset / signing pipeline you can extend the packaging script or add a second Meson custom target that invokes it. The official NDK “native-activity” sample and raylib’s Android wiki are the two best references for the low-level details.

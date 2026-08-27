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

---

## Camera

**Camera capture and live preview in pure C++23 with raylib on Android NativeActivity** using the NDK Camera2 API (`libcamera2ndk`) + `AImageReader` (from `libmediandk`). No Java/Kotlin sources are required for the capture path itself.

Raylib has no built-in Android camera support, so you capture frames yourself, convert them (usually YUV → RGBA), and feed them into a `Texture2D` for drawing.

### 1. Manifest additions
Add the permission and keep `android:hasCode="false"`:

```xml
<uses-permission android:name="android.permission.CAMERA" />
<!-- optional but recommended -->
<uses-feature android:name="android.hardware.camera" android:required="true" />
<uses-feature android:name="android.hardware.camera.autofocus" android:required="false" />
```

**Permission note (important for pure native):**  
Runtime permission requests normally require a tiny bit of Java/Kotlin (or a GameActivity helper). For a barebones NativeActivity build you can:
- Grant via `adb shell pm grant com.example.raylibcpp android.permission.CAMERA`
- Or assume the permission is already granted (common for dedicated/kiosk devices)
- Or add a minimal one-class Java permission helper

### 2. Link libraries (Meson)
In your `shared_library(...)` (or the equivalent link_args):

```meson
link_args: [
  # ... previous flags ...
  '-lcamera2ndk',
  '-lmediandk',
  '-landroid',
  '-llog',
  # ...
]
```

Also make sure the NDK include path is visible for:

```cpp
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImageReader.h>
#include <media/NdkImage.h>
```

### 3. Minimal camera helper (C++23)
Create something like `src/android_camera.hpp` / `.cpp`. Below is a concise, self-contained sketch that produces RGBA frames suitable for raylib.

```cpp
// android_camera.hpp (header-only sketch for clarity)
#pragma once
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <media/NdkImageReader.h>
#include <media/NdkImage.h>
#include <android/native_window.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

class AndroidCamera {
public:
    bool open(int width = 1280, int height = 720, int maxImages = 4);
    void close();
    bool isReady() const { return ready_.load(); }

    // Call from your render thread. Returns true if a new frame was copied.
    bool acquireLatestRGBA(std::vector<uint8_t>& outRgba, int& outW, int& outH);

private:
    static void onImageAvailable(void* ctx, AImageReader* reader);
    // ... device / session state callbacks (onDisconnected, onError, etc.)

    ACameraManager* mgr_ = nullptr;
    ACameraDevice* device_ = nullptr;
    ACaptureSession* session_ = nullptr;
    AImageReader* reader_ = nullptr;
    ANativeWindow* readerWindow_ = nullptr;
    ACaptureRequest* request_ = nullptr;

    std::mutex frameMtx_;
    std::vector<uint8_t> latestRgba_;
    int frameW_ = 0, frameH_ = 0;
    std::atomic<bool> ready_{false};
    std::atomic<bool> newFrame_{false};
};
```

Core implementation outline (simplified):

```cpp
bool AndroidCamera::open(int width, int height, int maxImages) {
    mgr_ = ACameraManager_create();
    // 1. Get camera ID list, pick back-facing (or first available)
    // 2. ACameraManager_openCamera(...)
    // 3. Create AImageReader with AIMAGE_FORMAT_YUV_420_888 (most compatible)
    //    or AIMAGE_FORMAT_RGBA_8888 if the device supports it for the chosen size
    AImageReader_new(width, height, AIMAGE_FORMAT_YUV_420_888, maxImages, &reader_);
    AImageReader_getWindow(reader_, &readerWindow_);
    AImageReader_setImageListener(reader_, &listener); // listener.context = this; listener.onImageAvailable = onImageAvailable;

    // 4. Create capture session output from the ImageReader window
    // 5. Create TEMPLATE_PREVIEW request, add the output target
    // 6. ACameraDevice_createCaptureSession + setRepeatingRequest
    ready_ = true;
    return true;
}

void AndroidCamera::onImageAvailable(void* ctx, AImageReader* reader) {
    auto* self = static_cast<AndroidCamera*>(ctx);
    AImage* image = nullptr;
    if (AImageReader_acquireLatestImage(reader, &image) != AMEDIA_OK) return;

    // YUV_420_888 → RGBA conversion (you need a small converter;
    // many open-source ones exist, or use libyuv if you want to vendor it)
    int w, h;
    AImage_getWidth(image, &w);
    AImage_getHeight(image, &h);
    // ... extract planes with AImage_getPlaneData / getPlaneRowStride / getPlanePixelStride
    // ... convert to tightly packed RGBA
    {
        std::lock_guard lock(self->frameMtx_);
        self->latestRgba_ = std::move(converted);
        self->frameW_ = w;
        self->frameH_ = h;
        self->newFrame_ = true;
    }
    AImage_delete(image);
}

bool AndroidCamera::acquireLatestRGBA(std::vector<uint8_t>& out, int& outW, int& outH) {
    if (!newFrame_.exchange(false)) return false;
    std::lock_guard lock(frameMtx_);
    out = latestRgba_;          // or swap
    outW = frameW_;
    outH = frameH_;
    return true;
}
```

You must implement the full set of state callbacks (`ACameraDevice_StateCallbacks`, `ACameraCaptureSession_stateCallbacks`) and clean up everything in `close()` (stop repeating request, close session, close device, delete reader, etc.).

### 4. Integration with raylib main loop
```cpp
#include "raylib.h"
#include "android_camera.hpp"

AndroidCamera cam;
Texture2D camTex = {0};
Image camImg = {0};
bool texReady = false;

int main() {
    InitWindow(0, 0, "raylib + camera");
    SetTargetFPS(60);
    SetExitKey(0);

    // Wait until the native window exists (raylib Android backend is ready)
    // then:
    if (!cam.open(1280, 720)) {
        // handle error
    }

    while (!WindowShouldClose()) {
        // Poll new camera frame
        std::vector<uint8_t> rgba;
        int w, h;
        if (cam.acquireLatestRGBA(rgba, w, h)) {
            if (!texReady || camImg.width != w || camImg.height != h) {
                if (texReady) {
                    UnloadTexture(camTex);
                    UnloadImage(camImg);
                }
                camImg = {
                    .data = rgba.data(),   // temporary; better to own the buffer
                    .width = w,
                    .height = h,
                    .mipmaps = 1,
                    .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
                };
                // Prefer: create Image with your own owned buffer, then
                camTex = LoadTextureFromImage(camImg);
                texReady = true;
            } else {
                // Update existing texture (fast path)
                UpdateTexture(camTex, rgba.data());
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);
        if (texReady) {
            // Scale to fit while preserving aspect
            float scale = fminf((float)GetScreenWidth() / camTex.width,
                                (float)GetScreenHeight() / camTex.height);
            DrawTextureEx(camTex, {0,0}, 0.0f, scale, WHITE);
        } else {
            DrawText("Waiting for camera...", 40, 40, 30, LIGHTGRAY);
        }
        DrawFPS(10, 10);
        EndDrawing();
    }

    cam.close();
    if (texReady) {
        UnloadTexture(camTex);
        // UnloadImage if you own the data
    }
    CloseWindow();
    return 0;
}
```

### 5. Practical tips & limitations
- **Format choice**: `AIMAGE_FORMAT_YUV_420_888` is the most widely supported. You need a YUV→RGBA converter (write a simple one or vendor a minimal libyuv). Some devices also support `AIMAGE_FORMAT_RGBA_8888` directly — query the stream configurations.
- **Resolution**: Always query supported sizes with `ACameraMetadata` / `ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS` and pick a size the device actually lists.
- **Performance**: Do the YUV conversion on a background thread if possible; keep the raylib thread light. `AImageReader_acquireLatestImage` drops old frames automatically.
- **Lifecycle**: Open the camera only after the `ANativeWindow` is available (after raylib has created the window). Close it on `APP_CMD_PAUSE` / destroy. Raylib’s Android backend already handles most activity lifecycle; just hook the camera open/close into the same places.
- **Orientation**: Camera sensor orientation is often 90°. Query `ACAMERA_SENSOR_ORIENTATION` and rotate the image or use a raylib transform.
- **Front vs back**: Enumerate cameras and check `ACAMERA_LENS_FACING`.
- **Still capture**: You can add a second `AImageReader` (JPEG) or a still-capture request on the same session when the user taps the screen.
- **Size / binary**: Adding the camera libs increases the `.so` only modestly. Keep using static raylib + static C++ runtime if you want a small APK.

### 6. Recommended references
- Official NDK camera samples: `android/ndk-samples` → `camera/basic` and `camera/texture-view` (the “basic” one is closest to pure native + ImageReader).
- Blog / clean examples: sisik.eu “Using Android Native Camera API”, logits-systems/Cam2Ndk, sixo/native-camera.
- Headers live under `$NDK/toolchains/llvm/prebuilt/.../sysroot/usr/include/camera/` and `media/`.

---

## Permission helper

**Minimal Java helper** for runtime `CAMERA` permission and native library loading.

### 1. `NativeLoader.java`
Place it at the path that matches your package, e.g.  
`android/src/com/example/raylibcpp/NativeLoader.java`  
(adjust package name to match your `AndroidManifest.xml`).

```java
package com.example.raylibcpp;

import android.Manifest;
import android.app.NativeActivity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;

public class NativeLoader extends NativeActivity {
    private static final String TAG = "NativeLoader";
    private static final int REQUEST_CAMERA = 1001;

    static {
        // Load your shared library (must match android.app.lib_name / libmain.so)
        System.loadLibrary("main");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Request camera permission before the native side tries to open the camera
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{ Manifest.permission.CAMERA }, REQUEST_CAMERA);
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_CAMERA) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                Log.i(TAG, "CAMERA permission granted");
                // Optional: notify native code that it is now safe to open the camera
                // nativeOnCameraPermissionGranted();
            } else {
                Log.e(TAG, "CAMERA permission denied");
                // Optional: finish() or show a message
            }
        }
    }

    // Optional native callbacks (uncomment + implement in C++ if you want)
    // private native void nativeOnCameraPermissionGranted();
    // private native void nativeOnCameraPermissionDenied();
}
```

### 2. Update `AndroidManifest.xml`
Change the activity name and keep the library meta-data:

```xml
<manifest ...>
    <uses-permission android:name="android.permission.CAMERA" />
    <uses-feature android:name="android.hardware.camera" android:required="true" />

    <application
        android:label="Raylib C++ Camera"
        android:hasCode="true"          <!-- now true because we have a Java class -->
        ... >

        <activity
            android:name="com.example.raylibcpp.NativeLoader"
            android:exported="true"
            android:configChanges="orientation|keyboardHidden|screenSize"
            android:screenOrientation="landscape"
            android:theme="@android:style/Theme.NoTitleBar.Fullscreen"
            android:launchMode="singleTask">

            <meta-data
                android:name="android.app.lib_name"
                android:value="main" />

            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

### 3. Packaging notes (aapt / your script)
Because you now have a Java class you must:

1. Compile the `.java` → `.class`
2. Convert to `classes.dex` (with `d8` or `dx`)
3. Include `classes.dex` in the APK

Minimal commands (adapt paths):

```bash
# Compile
javac -classpath $ANDROID_SDK/platforms/android-34/android.jar \
      -d android/obj \
      android/src/com/example/raylibcpp/NativeLoader.java

# Make dex
d8 --output android/build \
   android/obj/com/example/raylibcpp/NativeLoader.class

# Then when packaging with aapt:
aapt package ... -F unsigned.apk
aapt add unsigned.apk classes.dex
# + your lib/<abi>/libmain.so as before
```

(Your existing packaging script just needs these two extra steps.)

### 4. Optional: notify native code
If you want the C++ side to know the permission result, add the native methods in the Java class and declare them in C++:

```cpp
// In your C++ (or a small JNI file)
extern "C" {
    JNIEXPORT void JNICALL
    Java_com_example_raylibcpp_NativeLoader_nativeOnCameraPermissionGranted(JNIEnv*, jobject) {
        // set a global flag or call your AndroidCamera::open()
    }
}
```

Most projects simply check permission status from native code after the activity has started (or just attempt to open the camera and handle failure).

---

## QR code scanning

**QR code detection and processing on the live camera feed** in pure C++23 and raylib and NativeActivity setup. Use a lightweight native library (recommended: **zxing-cpp**) that operates directly on the RGBA (or YUV) frames you already acquire from `AImageReader`.

No Java/Kotlin is required.

### 1. Recommended library: zxing-cpp
- Modern C++17/20/23 compatible
- Excellent performance on mobile
- Supports QR + many other formats
- Easy to integrate as a Meson subproject or static library
- Works on raw grayscale / RGB / RGBA buffers

Alternatives if you want something even smaller:
- **quirc** (pure C, very small, QR-only)
- ZBar (older)

zxing-cpp is the best balance for a raylib project.

### 2. Meson integration
Add to your root `meson.build` (or a subdir):

```meson
# Option A: Meson wrap / subproject (preferred)
zxing_proj = subproject('zxing-cpp', required: true,
  default_options: ['readers=true', 'writers=false', 'experimental=false'])
zxing_dep = zxing_proj.get_variable('zxing_dep')  # adjust to actual variable name

# Option B: system / prebuilt / manual
# zxing_dep = dependency('zxing', required: false)
# or declare_dependency(include_directories: ..., link_with: ...)

# Then in your shared_library:
dependencies: [raylib_dep, zxing_dep],
```

Or vendor the source and build it as a static library target inside your project.

Link flags stay the same as the camera stage (`-lcamera2ndk -lmediandk` etc.).

### 3. Frame pipeline (camera → QR → raylib)
Extend the previous `AndroidCamera` so that after you convert a frame to RGBA you also run detection.

```cpp
#include <ZXing/ReadBarcode.h>
#include <ZXing/BarcodeFormat.h>
#include <ZXing/Result.h>
// or the newer ImageView / ReadBarcodes API depending on zxing-cpp version

struct QRResult {
    bool found = false;
    std::string text;
    // optional: std::vector<Point> corners; for drawing overlays
};

QRResult detectQR(const uint8_t* rgba, int width, int height) {
    QRResult res;

    // zxing-cpp expects a view. For RGBA:
    ZXing::ImageView image{rgba, width, height, ZXing::ImageFormat::RGBA};

    // or convert to luminance first for slightly better speed/accuracy on some devices
    // ZXing::ImageView image{gray.data(), width, height, ZXing::ImageFormat::Lum};

    auto options = ZXing::ReaderOptions()
        .setFormats(ZXing::BarcodeFormat::QRCode)  // restrict to QR for speed
        .setTryHarder(true)
        .setTryRotate(true)
        .setIsPure(false);

    auto results = ZXing::ReadBarcodes(image, options);  // or ReadBarcode for single

    if (!results.empty()) {
        res.found = true;
        res.text = results[0].text();
        // res.corners = results[0].position(); // if you want to draw the quad
    }
    return res;
}
```

In the main loop (after `cam.acquireLatestRGBA`):

```cpp
std::string lastQR;
double lastDetectTime = 0.0;
const double detectInterval = 0.15; // ~6–7 Hz is plenty; higher wastes CPU

// inside the loop
if (cam.acquireLatestRGBA(rgba, w, h)) {
    // update texture as before...
    UpdateTexture(camTex, rgba.data());

    // Throttle detection – camera may deliver 30 fps, you rarely need that rate
    double now = GetTime();
    if (now - lastDetectTime >= detectInterval) {
        auto qr = detectQR(rgba.data(), w, h);
        if (qr.found && qr.text != lastQR) {
            lastQR = qr.text;
            // do something: log, trigger action, show UI, etc.
            TraceLog(LOG_INFO, "QR: %s", lastQR.c_str());
        }
        lastDetectTime = now;
    }
}

// Drawing
BeginDrawing();
ClearBackground(BLACK);
if (texReady) {
    // draw camera texture scaled...
    DrawTextureEx(camTex, {0,0}, 0.0f, scale, WHITE);
}
if (!lastQR.empty()) {
    DrawRectangle(0, GetScreenHeight()-80, GetScreenWidth(), 80, Fade(BLACK, 0.7f));
    DrawText(lastQR.c_str(), 20, GetScreenHeight()-55, 32, GREEN);
}
DrawFPS(10, 10);
EndDrawing();
```

### 4. Performance tips for mobile
- Restrict formats to `BarcodeFormat::QRCode` only.
- Run detection every 100–200 ms instead of every camera frame.
- Prefer a moderate resolution for the `AImageReader` (720p or even 640×480 is usually enough for QR).
- Optional: convert RGBA → grayscale once and feed luminance to zxing (slightly faster).
- Keep the conversion + detection off the absolute critical path if you later add heavier processing; a simple worker thread + lock-free / double-buffer queue works well.
- zxing-cpp’s `TryHarder` / `TryRotate` help with awkward angles but cost CPU — tune them.

### 5. Optional enhancements
- **Overlay the QR quad**: zxing returns corner points. Map them through the same scale/offset you use for the texture and draw lines with `DrawLineEx`.
- **Multiple codes**: use `ReadBarcodes` and iterate.
- **Action on new code**: debounce so the same code doesn’t fire repeatedly (compare with `lastQR` + a short cooldown).
- **Still capture + high-res QR**: on button press, issue a still-capture request (higher resolution JPEG or YUV) and run detection on that single frame.
- **Front camera / orientation**: apply the sensor orientation rotation before detection or let zxing’s `TryRotate` handle it.

### 6. Build / packaging reminders
- Add the zxing-cpp sources or wrap file to your Meson setup.
- No extra Android permissions beyond the existing `CAMERA`.
- The resulting `libmain.so` grows by a few hundred KB (static zxing-cpp is modest).
- Test on a real device; emulator camera behavior can be limited.


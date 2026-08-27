#!/usr/bin/env bash

export ANDROID_NDK="$HOME/opt/android/sdk/ndk/29.0.14206865"
export ANDROID_SDK="$HOME/opt/android/sdk"
export ANDROID_HOME="$HOME/opt/android"
export ANDROID_PLATFORM=$ANDROID_SDK/platforms/android-34

export JAVA_HOME="$HOME/opt/jdk/current"

export PATH="$PATH:$HOME/opt/jdk/current/bin:$HOME/opt/android/sdk/platform-tools"
export PATH="$PATH:$HOME/opt/android/sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin"
export PATH="$PATH:$HOME/opt/android/sdk/build-tools/34.0.0"

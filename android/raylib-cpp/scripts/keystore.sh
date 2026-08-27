#!/usr/bin/env bash

# 1. Create a keystore
#For a development/debug APK:

# generic
keytool -genkeypair \
  -v \
  -keystore android.keystore \
  -alias android \
  -keyalg RSA \
  -keysize 2048 \
  -validity 10000

keytool -genkeypair \
  -v -keystore android.keystore \
  -alias android \
  -keyalg RSA \
  -keysize 2048 -validity 10000 \
  -storepass android -keypass android \
  -dname "CN=RayLib App, OU=Dev, O=Independent, L=Quezon City, ST=Metro Manila, C=PH"

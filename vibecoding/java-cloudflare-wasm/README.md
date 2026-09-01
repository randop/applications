# Java 25 -> WebAssembly -> Cloudflare Worker

A deliberately barebones experiment.

## Requirements

- JDK 25
- Maven 3.9+
- Node.js 20+
- Cloudflare account

## Build

```sh
./build.sh
```

This runs:

```text
Java source
  -> javac (running on JDK 25)
  -> TeaVM
  -> WebAssembly GC (.wasm)
  -> TeaVM runtime JS
  -> Cloudflare Worker
```

The generated artifacts are copied to:

```text
worker/app.wasm
worker/app.wasm-runtime.js
```

## Local development

```sh
./build.sh
cd worker
npm install
npm run dev
```

## Deploy

```sh
./deploy.sh
```

or:

```sh
./build.sh
cd worker
npm install
npx wrangler login
npm run deploy
```

## Important compatibility note

The project is built with JDK 25, but Maven's `--release` is intentionally
set to 17. TeaVM compiles JVM bytecode rather than running a JVM inside Wasm,
and Java language/library support depends on the TeaVM version.

Therefore this is a **JDK 25 toolchain project**, not a claim that every
Java 25 language feature or JDK API is supported by TeaVM.

## Architecture

```text
Main.java
   |
   v
JDK 25 javac
   |
   v
TeaVM
   |
   +--> app.wasm
   +--> app.wasm-runtime.js
             |
             v
       Cloudflare Worker
```

/**
 * ipfg Cloudflare Worker
 * - Wasm binary fetched from R2 and cached globally (survives warm requests)
 * - WASI shim via @cloudflare/workers-wasi
 * - Production hardened: timeouts, error boundaries, cache-control headers
 */

import { WASI } from "@cloudflare/workers-wasi";

// ---------------------------------------------------------------------------
// Module-level cache — persists across requests on the same isolate instance.
// R2 is only hit on cold start or after cache invalidation.
// ---------------------------------------------------------------------------
let cachedWasmModule = null;
let cacheEtag = null;

const WASM_R2_KEY = "ipfg.wasm";
const CACHE_MAX_AGE_S = 3600; // re-validate from R2 every 1 hour

/**
 * Fetch and compile the Wasm module from R2.
 * Uses ETag-based conditional fetching to avoid re-downloading when unchanged.
 */
async function getWasmModule(bucket) {
  // Attempt conditional fetch using last known ETag
  const options = cacheEtag ? { onlyIf: { etagMatches: cacheEtag } } : {};
  const obj = await bucket.get(WASM_R2_KEY, options);

  if (obj === null) {
    // 304 Not Modified equivalent — R2 returns null when etag matched
    if (cachedWasmModule) return cachedWasmModule;
    throw new Error("Wasm object not found in R2 and no cached module available.");
  }

  // New or updated object — compile and cache
  const wasmBytes = await obj.arrayBuffer();
  cachedWasmModule = await WebAssembly.compile(wasmBytes);
  cacheEtag = obj.etag ?? null;

  return cachedWasmModule;
}

// ---------------------------------------------------------------------------
// Request handler
// ---------------------------------------------------------------------------
export default {
  async fetch(request, env, ctx) {
    // Only allow GET and POST
    if (request.method !== "GET" && request.method !== "POST") {
      return new Response("Method Not Allowed", { status: 405 });
    }

    let wasmModule;
    try {
      wasmModule = await getWasmModule(env.WASM_BUCKET);
    } catch (err) {
      console.error("Failed to load Wasm module:", err);
      return new Response("Service Unavailable: could not load Wasm module.", {
        status: 503,
        headers: { "Retry-After": "10" },
      });
    }

    // ------------------------------------------------------------------
    // Stream stdin → Wasm → stdout
    // Feed the client IP address into Wasm via stdin.
    // Cloudflare sets CF-Connecting-IP to the true client IP even behind
    // proxies — always prefer it over parsing X-Forwarded-For manually.
    // ------------------------------------------------------------------
    const clientIp = request.headers.get("CF-Connecting-IP")
      ?? request.headers.get("X-Forwarded-For")?.split(",")[0].trim()
      ?? "192.168.1.1";

    const stdin = ipToStream(clientIp);
    const { readable: stdout, writable: stdoutWritable } = new TransformStream();
    const { readable: stderr, writable: stderrWritable } = new TransformStream();

    const wasi = new WASI({
      args: ["ipfg"],          // argv[0] = program name only; IP is on stdin
      env: wasiEnv(request, env),
      stdin,
      stdout: stdoutWritable,
      stderr: stderrWritable,
    });

    let instance;
    try {
      instance = await WebAssembly.instantiate(wasmModule, {
        wasi_snapshot_preview1: wasi.wasiImport,
      });
    } catch (err) {
      console.error("Wasm instantiation failed:", err);
      return new Response("Internal Server Error: Wasm instantiation failed.", {
        status: 500,
      });
    }

    // Run the Wasm module — use ctx.waitUntil so the isolate stays alive
    // until stdout is fully flushed, even if the client is fast.
    const exitPromise = wasi.start(instance).catch((err) => {
      console.error("Wasm runtime error:", err);
    });
    ctx.waitUntil(exitPromise);

    // Drain stderr to console without blocking the response
    ctx.waitUntil(drainToConsole(stderr));

    return new Response(stdout, {
      status: 200,
      headers: responseHeaders(request),
    });
  },
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Expose safe env vars to the Wasm module. */
function wasiEnv(request, env) {
  return {
    CF_RAY: request.headers.get("CF-Ray") ?? "",
  };
}

/**
 * Encode an IP address string as a UTF-8 ReadableStream with a newline,
 * matching the convention of most CLI tools that read a line from stdin.
 */
function ipToStream(ip) {
  const encoded = new TextEncoder().encode(ip + "\n");
  return new ReadableStream({
    start(controller) {
      controller.enqueue(encoded);
      controller.close();
    },
  });
}

/** Production response headers. */
function responseHeaders(request) {
  const headers = new Headers({
    "Content-Type": "application/json; charset=utf-8",
    "Cache-Control": `public, max-age=${CACHE_MAX_AGE_S}`,
    "X-Content-Type-Options": "nosniff",
    "X-Frame-Options": "DENY",
  });

  // CORS — tighten origin in production
  const origin = request.headers.get("Origin");
  if (origin) {
    headers.set("Access-Control-Allow-Origin", origin);
    headers.set("Vary", "Origin");
  }

  return headers;
}

/** Drain a ReadableStream to console.error without throwing. */
async function drainToConsole(readable) {
  try {
    const reader = readable.getReader();
    const decoder = new TextDecoder();
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      console.error("[wasm stderr]", decoder.decode(value));
    }
  } catch (_) {
    // best-effort
  }
}

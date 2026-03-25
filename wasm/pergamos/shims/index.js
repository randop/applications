import { WASI } from "@cloudflare/workers-wasi";

import wasmModule from '../.build-wasm/pergamos.wasm';

const CACHE_MAX_AGE_S = 3600; // re-validate from R2 every 1 hour

export default {
  async fetch(request, env, ctx) {
    // Only allow GET and POST
    if (request.method !== "GET" && request.method !== "POST") {
      return new Response("Method Not Allowed", { status: 405 });
    }

    // ------------------------------------------------------------------
    // Stream stdin → Wasm → stdout
    // ------------------------------------------------------------------
    const clientIp = request.headers.get("CF-Connecting-IP")
      ?? request.headers.get("X-Forwarded-For")?.split(",")[0].trim()
      ?? "192.168.1.1";

    const stdin = stringToStream(clientIp);
    const { readable: stdout, writable: stdoutWritable } = new TransformStream();
    const { readable: stderr, writable: stderrWritable } = new TransformStream();

    const wasi = new WASI({
      args: ["pergamos"],
      env: wasiEnv(request, env),
      stdin,
      stdout: stdoutWritable,
      stderr: stderrWritable,
    });

    const imports = {
      wasi_snapshot_preview1: {
        ...wasi.wasiImport,
        // Stub the missing socket functions (they are rarely used in simple WASI apps)
        sock_accept: () => {
          console.error('sock_accept called - not implemented in Cloudflare WASI');
          return 0; // or throw if you prefer strict failure
        },
        sock_recv: () => 0,
        sock_send: () => 0,
        sock_shutdown: () => 0,
        sock_close: () => 0,
        sock_open: () => 0,
      },
    };

    let instance;
    try {
      instance = new WebAssembly.Instance(wasmModule, imports);
    } catch (err) {
      console.error("Wasm instantiation failed:", err);
      return new Response("Internal Server Error.", {
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
 * Encode a string as a UTF-8 ReadableStream with a newline,
 * matching the convention of most CLI tools that read a line from stdin.
 */
function stringToStream(s) {
  const encoded = new TextEncoder().encode(s + "\n");
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

/** Drain a ReadableStream without throwing. */
async function drainToConsole(readable) {
  try {
    const reader = readable.getReader();
    while (true) {
      const { done } = await reader.read();
      if (done) break;
    }
  } catch (_) {
    // best-effort
  }
}

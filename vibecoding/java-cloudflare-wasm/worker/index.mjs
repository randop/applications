import "./app.wasm-runtime.js";

/*
 * TeaVM's generated runtime exposes TeaVM.wasmGC globally.
 * Cloudflare Workers support precompiled WebAssembly modules and ES modules.
 */
const teavm = await TeaVM.wasmGC.load(new URL("./app.wasm", import.meta.url));

export default {
  async fetch(request) {
    const url = new URL(request.url);

    if (url.pathname === "/") {
      return new Response(
        "Java 25 toolchain -> TeaVM WebAssembly -> Cloudflare Worker\n",
        { headers: { "content-type": "text/plain; charset=utf-8" } }
      );
    }

    return new Response("Not found\n", { status: 404 });
  }
};

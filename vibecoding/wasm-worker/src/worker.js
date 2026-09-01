// src/worker.js
//
// Cloudflare Worker entry point. This is the actual "Worker" Cloudflare
// runs (JS, on V8) -- it terminates fetch events, does routing, and calls
// into the compiled currency.wasm module for conversion math. Wrangler
// bundles the .wasm import below into a WebAssembly.Module automatically.

import { graphql } from "graphql";
import { schema, createRootValue } from "./graphql/schema.js";
import { TursoClient } from "./db/turso.js";
import { CurrencyEngine } from "./wasm/currency.js";

// Wrangler treats a direct `.wasm` import as a WebAssembly.Module binding.
import wasmModule from "../wasm/currency.wasm";

const rootValue = createRootValue();

// The compiled module is stateless (pure functions over an arena that's
// reset per request), so a single instance can be reused across requests
// within the same isolate.
let enginePromise;
function getEngine() {
  if (!enginePromise) enginePromise = CurrencyEngine.load(wasmModule);
  return enginePromise;
}

const CORS_HEADERS = {
  "access-control-allow-origin": "*",
  "access-control-allow-methods": "GET, POST, OPTIONS",
  "access-control-allow-headers": "content-type",
};

export default {
  /** @param {Request} request @param {{TURSO_DATABASE_URL: string, TURSO_AUTH_TOKEN: string}} env */
  async fetch(request, env) {
    const url = new URL(request.url);

    if (request.method === "OPTIONS") {
      return new Response(null, { headers: CORS_HEADERS });
    }

    if (url.pathname === "/" || url.pathname === "/graphiql") {
      return new Response(GRAPHIQL_HTML, {
        headers: { "content-type": "text/html; charset=utf-8" },
      });
    }

    if (url.pathname === "/healthz") {
      return new Response("ok", { headers: CORS_HEADERS });
    }

    if (url.pathname === "/graphql") {
      return handleGraphQL(request, env);
    }

    return new Response("Not found", { status: 404, headers: CORS_HEADERS });
  },
};

async function handleGraphQL(request, env) {
  let query, variables, operationName;

  if (request.method === "GET") {
    const url = new URL(request.url);
    query = url.searchParams.get("query");
    variables = safeJsonParse(url.searchParams.get("variables")) ?? undefined;
    operationName = url.searchParams.get("operationName") ?? undefined;
  } else if (request.method === "POST") {
    const contentType = request.headers.get("content-type") ?? "";
    if (contentType.includes("application/json")) {
      const body = await request.json().catch(() => ({}));
      ({ query, variables, operationName } = body);
    } else if (contentType.includes("application/graphql")) {
      query = await request.text();
    } else {
      return jsonResponse({ errors: [{ message: "Unsupported content-type" }] }, 415);
    }
  } else {
    return jsonResponse({ errors: [{ message: "Method not allowed" }] }, 405);
  }

  if (!query) {
    return jsonResponse({ errors: [{ message: "Missing GraphQL query" }] }, 400);
  }

  if (!env.TURSO_DATABASE_URL || !env.TURSO_AUTH_TOKEN) {
    return jsonResponse(
      { errors: [{ message: "Server misconfigured: TURSO_DATABASE_URL/TURSO_AUTH_TOKEN not set" }] },
      500
    );
  }

  const db = new TursoClient(env.TURSO_DATABASE_URL, env.TURSO_AUTH_TOKEN);
  const engine = await getEngine();

  const result = await graphql({
    schema,
    source: query,
    rootValue,
    variableValues: variables,
    operationName,
    contextValue: { db, engine },
  });

  return jsonResponse(result, 200);
}

function jsonResponse(body, status) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json", ...CORS_HEADERS },
  });
}

function safeJsonParse(text) {
  if (!text) return undefined;
  try {
    return JSON.parse(text);
  } catch {
    return undefined;
  }
}

const GRAPHIQL_HTML = `<!DOCTYPE html>
<html>
<head>
  <title>Currency Conversion GraphQL API</title>
  <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/graphiql/3.2.0/graphiql.min.css" />
  <style>html, body, #graphiql { height: 100%; margin: 0; }</style>
</head>
<body>
  <div id="graphiql">Loading GraphiQL...</div>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/react/18.3.1/umd/react.production.min.js"></script>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/react-dom/18.3.1/umd/react-dom.production.min.js"></script>
  <script src="https://cdnjs.cloudflare.com/ajax/libs/graphiql/3.2.0/graphiql.min.js"></script>
  <script>
    const fetcher = GraphiQL.createFetcher({ url: '/graphql' });
    const root = ReactDOM.createRoot(document.getElementById('graphiql'));
    root.render(React.createElement(GraphiQL, {
      fetcher,
      defaultQuery: [
        '# Try it:',
        'query Convert100UsdToEur {',
        '  convert(amount: 100, from: "USD", to: "EUR") {',
        '    result',
        '    method',
        '    effectiveRate',
        '    to { code symbol }',
        '  }',
        '}',
      ].join('\\n'),
    }));
  </script>
</body>
</html>`;

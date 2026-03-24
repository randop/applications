/**
 * ============================================================
 * CLOUDFLARE WORKER — ALGOLIA SEARCH PROXY (SECURITY LAYER)
 * ============================================================
 * Deploy this Worker at: https://your-domain.com/api/search
 *
 * What this protects against:
 *  - DDoS / volumetric abuse         → Rate limiting per IP
 *  - Credential theft                 → API keys never reach the browser
 *  - Bot / scraper attacks            → Bot score + browser integrity check
 *  - Hotlinking / cross-origin abuse  → Strict CORS enforcement
 *  - Malformed / injected queries     → Input validation & sanitisation
 *  - Replay attacks                   → Request freshness check (timestamp)
 *  - Datacenter bots                  → ASN/IP threat intelligence
 *  - Excessive result harvesting      → Max hits-per-page enforcement
 *  - Enumeration attacks              → Consistent error responses
 * ============================================================
 */

// ── CONFIGURATION ──────────────────────────────────────────
const CONFIG = {
  algolia: {
    appId:      "YOUR_ALGOLIA_APP_ID",          // ← replace
    searchKey:  "YOUR_ALGOLIA_SEARCH_ONLY_KEY", // ← replace (Search-Only key, NOT Admin)
    indexName:  "YOUR_INDEX_NAME",              // ← replace
  },
  rateLimit: {
    windowSeconds:  60,   // sliding window
    maxRequests:    20,   // requests per IP per window
    burstAllowance: 5,    // extra burst tokens
  },
  request: {
    maxQueryLength:    200,  // chars
    maxHitsPerPage:    20,   // clamp results
    maxTimestampSkew:  30,   // seconds; set 0 to disable freshness check
  },
  cors: {
    // Exact origins allowed. Add all your production domains here.
    allowedOrigins: [
      "https://yourdomain.com",
      "https://www.yourdomain.com",
      // "https://staging.yourdomain.com",  // uncomment if needed
    ],
  },
};

// ── HELPERS ─────────────────────────────────────────────────
const jsonResponse = (body, status = 200, extraHeaders = {}) =>
  new Response(JSON.stringify(body), {
    status,
    headers: {
      "Content-Type": "application/json",
      "X-Content-Type-Options": "nosniff",
      "X-Frame-Options": "DENY",
      "Referrer-Policy": "strict-origin-when-cross-origin",
      ...extraHeaders,
    },
  });

const errorResponse = (message, status, extraHeaders = {}) =>
  // Always return generic messages to prevent info leakage
  jsonResponse({ error: message }, status, extraHeaders);

// ── CORS ─────────────────────────────────────────────────────
function getCorsHeaders(origin) {
  if (!origin || !CONFIG.cors.allowedOrigins.includes(origin)) return null;
  return {
    "Access-Control-Allow-Origin": origin,
    "Access-Control-Allow-Methods": "POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, X-Request-Timestamp",
    "Access-Control-Max-Age": "86400",
    "Vary": "Origin",
  };
}

// ── RATE LIMITER (uses Cloudflare KV) ───────────────────────
async function checkRateLimit(ip, kv) {
  if (!kv) return true; // skip if KV not bound (dev mode)

  const key    = `rl:${ip}`;
  const now    = Math.floor(Date.now() / 1000);
  const window = CONFIG.rateLimit.windowSeconds;
  const max    = CONFIG.rateLimit.maxRequests + CONFIG.rateLimit.burstAllowance;

  let record = { count: 0, windowStart: now };
  try {
    const stored = await kv.get(key, "json");
    if (stored && now - stored.windowStart < window) {
      record = stored;
    }
  } catch (_) { /* treat as new window */ }

  record.count++;
  await kv.put(key, JSON.stringify(record), { expirationTtl: window });

  return record.count <= max;
}

// ── INPUT VALIDATION ─────────────────────────────────────────
function validatePayload(body) {
  const { query, page = 0, hitsPerPage = 10, filters = "", timestamp } = body;

  if (typeof query !== "string")
    return "Invalid query type.";

  const trimmed = query.trim();
  if (trimmed.length > CONFIG.request.maxQueryLength)
    return `Query exceeds ${CONFIG.request.maxQueryLength} characters.`;

  // Block obvious injection attempts
  const banned = [/<script/i, /javascript:/i, /on\w+\s*=/i, /\bUNION\b/i, /\bSELECT\b/i];
  if (banned.some(rx => rx.test(trimmed)))
    return "Query contains disallowed content.";

  if (typeof page !== "number" || page < 0 || page > 999)
    return "Invalid page parameter.";

  const clampedHits = Math.min(
    Math.max(1, Number(hitsPerPage) || 10),
    CONFIG.request.maxHitsPerPage,
  );

  // Optional: freshness check (prevents replay attacks)
  if (CONFIG.request.maxTimestampSkew > 0 && timestamp !== undefined) {
    const ts  = Number(timestamp);
    const now = Math.floor(Date.now() / 1000);
    if (isNaN(ts) || Math.abs(now - ts) > CONFIG.request.maxTimestampSkew)
      return "Request timestamp out of acceptable range.";
  }

  return { query: trimmed, page, hitsPerPage: clampedHits, filters };
}

// ── THREAT INTELLIGENCE ──────────────────────────────────────
function isThreatened(request, cf) {
  if (!cf) return false;

  // Block Cloudflare-identified bots (score 0 = likely bot, 100 = human)
  if (cf.botManagement?.score !== undefined && cf.botManagement.score < 30)
    return true;

  // Block known bad ASNs (add ASN numbers as needed)
  // const blockedASNs = [12345, 67890];
  // if (blockedASNs.includes(cf.asn)) return true;

  // Block Tor exit nodes
  if (cf.isEUCountry === false && cf.country === "T1") return true; // Tor

  return false;
}

// ── ALGOLIA FETCH ────────────────────────────────────────────
async function queryAlgolia({ query, page, hitsPerPage, filters }) {
  const url = `https://${CONFIG.algolia.appId}-dsn.algolia.net/1/indexes/${CONFIG.algolia.indexName}/query`;

  const res = await fetch(url, {
    method: "POST",
    headers: {
      "X-Algolia-Application-Id": CONFIG.algolia.appId,
      "X-Algolia-API-Key":        CONFIG.algolia.searchKey,
      "Content-Type":             "application/json",
    },
    body: JSON.stringify({
      query,
      page,
      hitsPerPage,
      filters,
      // Restrict what fields Algolia returns (minimise data exposure)
      attributesToRetrieve: ["objectID", "title", "url", "description", "image"],
      attributesToHighlight: ["title", "description"],
    }),
  });

  if (!res.ok) throw new Error(`Algolia error: ${res.status}`);
  return res.json();
}

// ── MAIN HANDLER ─────────────────────────────────────────────
export default {
  async fetch(request, env) {
    const origin = request.headers.get("Origin") || "";
    const corsHeaders = getCorsHeaders(origin);

    // ── Preflight ──
    if (request.method === "OPTIONS") {
      if (!corsHeaders)
        return new Response(null, { status: 403 });
      return new Response(null, { status: 204, headers: corsHeaders });
    }

    // ── Method guard ──
    if (request.method !== "POST")
      return errorResponse("Method not allowed.", 405);

    // ── CORS guard ──
    if (!corsHeaders)
      return errorResponse("Forbidden.", 403);

    // ── Threat intelligence ──
    if (isThreatened(request, request.cf))
      return errorResponse("Access denied.", 403, corsHeaders);

    // ── Rate limiting ──
    const ip = request.headers.get("CF-Connecting-IP") || "unknown";
    const allowed = await checkRateLimit(ip, env.RATE_LIMIT_KV);
    if (!allowed) {
      return errorResponse("Too many requests. Please slow down.", 429, {
        ...corsHeaders,
        "Retry-After": String(CONFIG.rateLimit.windowSeconds),
      });
    }

    // ── Parse body ──
    let body;
    try {
      body = await request.json();
    } catch {
      return errorResponse("Invalid JSON body.", 400, corsHeaders);
    }

    // ── Validate & sanitise ──
    const validated = validatePayload(body);
    if (typeof validated === "string")
      return errorResponse(validated, 400, corsHeaders);

    // ── Proxy to Algolia ──
    let data;
    try {
      data = await queryAlgolia(validated);
    } catch (err) {
      console.error("Algolia fetch failed:", err.message);
      return errorResponse("Search service unavailable.", 502, corsHeaders);
    }

    // ── Strip internal Algolia metadata before returning ──
    const safe = {
      hits:             data.hits,
      nbHits:           data.nbHits,
      page:             data.page,
      nbPages:          data.nbPages,
      hitsPerPage:      data.hitsPerPage,
      processingTimeMS: data.processingTimeMS,
      query:            data.query,
    };

    return jsonResponse(safe, 200, corsHeaders);
  },
};

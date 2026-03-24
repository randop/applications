# WebSearch

**Web search security architecture**

---

## Cybersecurity Architecture

### The core security model: **API keys never reach the browser**
Your Algolia Search-Only key lives exclusively inside the Cloudflare Worker. The browser calls `/api/search` on your own domain — it never touches Algolia directly.

---

### `algolia-proxy-worker.js` — The gatekeeper
A Cloudflare Worker that enforces **9 security layers**:

| Layer | Protection |
|---|---|
| **CORS enforcement** | Only your whitelisted origins can call the endpoint |
| **Method guard** | Only POST requests pass through |
| **Bot score check** | Rejects requests with `cf.botManagement.score < 30` |
| **Rate limiting** | 20 req/60s per IP using Cloudflare KV (sliding window) |
| **Input validation** | Max 200 chars, no `<script>`, no SQL injection patterns |
| **Timestamp freshness** | Optional 30s window blocks replayed requests |
| **Max hits clamping** | Can't harvest more than 20 results per call |
| **Response scrubbing** | Strips internal Algolia metadata before returning |
| **Error normalisation** | Generic error messages prevent information leakage |

### `wrangler.toml` — Deployment config
Binds the Worker to your domain route and the KV store used for rate limiting.

### `search.html` — Hardened frontend
- **No API keys** anywhere in client-side code
- **350ms debounce** on keystrokes (massively reduces request volume)
- **Client-side input cap** (maxlength=200) before anything hits the network
- **XSS-safe rendering** — all output is escaped; only Algolia's `<em>` highlight tags are allowed through
- **429 handling** with friendly retry countdown

### `cloudflare-security-rules.md` — Dashboard configuration
Step-by-step WAF rules, rate limiting, Bot Fight Mode, DDoS sensitivity override, SSL/TLS hardening, and Algolia dashboard hardening (restrict the key to your index only).

---

### Deployment in 4 commands
```bash
npm install -g wrangler
npx wrangler kv:namespace create RATE_LIMIT_KV   # paste ID into wrangler.toml
# fill in your appId, indexName, searchKey in the worker file
npx wrangler deploy
```

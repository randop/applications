# ============================================================
# CLOUDFLARE DASHBOARD — WAF & SECURITY RULES
# Apply these in: Cloudflare Dashboard → your zone
# ============================================================

# ─────────────────────────────────────────────────────────────
# 1. WAF CUSTOM RULES  (Security → WAF → Custom Rules)
# ─────────────────────────────────────────────────────────────

## Rule 1 — Block non-POST requests to the search proxy
Name:       "Block non-POST to /api/search"
Expression: (http.request.uri.path eq "/api/search" and http.request.method ne "POST")
Action:     Block

## Rule 2 — Block requests without JSON content-type
Name:       "Require JSON content-type on /api/search"
Expression: (http.request.uri.path eq "/api/search" and not http.request.headers["content-type"][0] contains "application/json")
Action:     Block

## Rule 3 — Block requests with no User-Agent (bots that forget headers)
Name:       "Block no User-Agent on /api/search"
Expression: (http.request.uri.path eq "/api/search" and http.request.headers["user-agent"] eq "")
Action:     Block

## Rule 4 — Block known malicious query strings at WAF level (pre-Worker)
Name:       "Block injection patterns"
Expression: (http.request.uri.path eq "/api/search" and
             (http.request.body contains "<script" or
              http.request.body contains "javascript:" or
              http.request.body contains "UNION SELECT" or
              http.request.body contains "../.."))
Action:     Block

## Rule 5 — Challenge suspicious countries (adjust as needed)
## Tip: start with "Managed Challenge" before going to "Block"
# Name:       "Country challenge"
# Expression: (http.request.uri.path eq "/api/search" and ip.geoip.country in {"CN" "RU" "KP"})
# Action:     Managed Challenge


# ─────────────────────────────────────────────────────────────
# 2. RATE LIMITING  (Security → WAF → Rate Limiting Rules)
# ─────────────────────────────────────────────────────────────

## Rule — IP rate limit on /api/search
Name:             "Search rate limit"
Criteria:         URI path equals /api/search
Rate:             30 requests per 60 seconds per IP
Mitigation:       Block (return 429)
Response:         Custom JSON: {"error":"Too many requests."}
Mitigation Period: 60 seconds

# The Worker also enforces its own rate limit (20 req/60s).
# Two layers = belt AND suspenders.


# ─────────────────────────────────────────────────────────────
# 3. MANAGED RULES  (Security → WAF → Managed Rules)
# ─────────────────────────────────────────────────────────────
Enable the Cloudflare Managed Ruleset (free + paid):
  ✅ Cloudflare Free Managed Ruleset
  ✅ Cloudflare OWASP Core Ruleset  (Pro+)
     → Set Paranoia Level: 2 (good balance of protection vs false positives)
     → Set Score Threshold: 60

# ─────────────────────────────────────────────────────────────
# 4. BOT MANAGEMENT  (Security → Bots)
# ─────────────────────────────────────────────────────────────
Free tier:
  ✅ Bot Fight Mode → ON

Pro/Business/Enterprise:
  ✅ Super Bot Fight Mode → ON
     → Definitely automated → Block
     → Likely automated     → Managed Challenge
     → Verified bots        → Allow (lets Google, Bing index your pages)

The Worker additionally checks cf.botManagement.score < 30.

# ─────────────────────────────────────────────────────────────
# 5. DDoS PROTECTION  (Security → DDoS)
# ─────────────────────────────────────────────────────────────
HTTP DDoS Attack Protection:
  ✅ Enable (always on, automatic for all plans)
  Override ruleset sensitivity → High for /api/search path

# ─────────────────────────────────────────────────────────────
# 6. SSL/TLS  (SSL/TLS → Overview)
# ─────────────────────────────────────────────────────────────
  ✅ Mode: Full (Strict)
  ✅ Always Use HTTPS → ON
  ✅ Minimum TLS Version → 1.2
  ✅ TLS 1.3 → ON
  ✅ Automatic HTTPS Rewrites → ON
  ✅ HSTS → Enable
       Max-Age: 1 year
       Include subdomains: Yes
       Preload: Yes (only after testing!)


# ─────────────────────────────────────────────────────────────
# 7. SECURITY HEADERS  (via Worker response headers, or Rules)
# ─────────────────────────────────────────────────────────────
Add these headers via a Transform Rule or your Worker:
  Content-Security-Policy:   default-src 'self'; script-src 'self'; object-src 'none'
  X-Content-Type-Options:    nosniff
  X-Frame-Options:           DENY
  Referrer-Policy:           strict-origin-when-cross-origin
  Permissions-Policy:        interest-cohort=()

# ─────────────────────────────────────────────────────────────
# 8. CACHE (Performance → Caching)
# ─────────────────────────────────────────────────────────────
Cache Rule — do NOT cache search results:
  Expression: (http.request.uri.path eq "/api/search")
  Cache:      Bypass


# ─────────────────────────────────────────────────────────────
# 9. ALGOLIA DASHBOARD HARDENING
# ─────────────────────────────────────────────────────────────
In your Algolia dashboard (algolia.com):

  a) API Key restrictions (Settings → API Keys)
     - Create a SEARCH-ONLY key (not the Admin key!)
     - Restrict key to specific index: YOUR_INDEX_NAME
     - Restrict key to specific HTTP referrers: NONE
       (since calls come from your Worker, not a browser)
     - Set Rate limit: 100 req/s (server-to-server is fine)
     - Set Max hits per query: 20

  b) Index settings (Indices → YOUR_INDEX → Configuration)
     - Attributes for faceting → only expose what you need
     - Unretrievable attributes → list any field that must stay private

  c) Allowed sources (if using Cloudflare IP ranges)
     - Optionally allowlist Cloudflare IP ranges at Algolia network level


# ─────────────────────────────────────────────────────────────
# 10. DEPLOYMENT CHECKLIST
# ─────────────────────────────────────────────────────────────
[ ] algolia-proxy-worker.js → fill in appId, indexName, search-only key
[ ] wrangler.toml → fill in zone_name, KV namespace ID
[ ] npx wrangler kv:namespace create RATE_LIMIT_KV  (and paste ID)
[ ] npx wrangler deploy
[ ] WAF rules created in Cloudflare dashboard
[ ] Bot Fight Mode enabled
[ ] DDoS High sensitivity override on /api/search
[ ] SSL Full (Strict) + Always HTTPS
[ ] HSTS enabled
[ ] Algolia key restricted to search-only + index-scoped
[ ] Test: curl -X POST https://yourdomain.com/api/search \
       -H 'Content-Type: application/json' \
       -d '{"query":"hello"}' → should return results
[ ] Test: rapid fire 25 requests → should receive 429 on excess
[ ] Test: curl from different Origin → should receive 403

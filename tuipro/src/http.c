/*
 * libuv + OpenSSL HTTP/1.1 client.
 *
 * The native side owns the network state machine, TLS, redirects and response
 * parsing.  It reports progress to a Lua callback table:
 *
 *   on_headers(status, status_text, flat_headers, url, redirected)
 *   on_data(chunk)
 *   on_end()
 *   on_error(message, code)
 *
 * All work happens on the thread that owns the libuv loop.  Handles are closed
 * asynchronously; a request is only freed from its final handle close callback,
 * which keeps callbacks and Lua registry references valid for their lifetime.
 */
#include "http.h"

#include "tuipro.h"

#include <lauxlib.h>
#include <lua.h>

#include <uv.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TUIPRO_HAVE_ZLIB
#include <zlib.h>
#endif

#define HTTP_MAX_HEADER (64 * 1024)
#define HTTP_MAX_LINE (8 * 1024)
#define HTTP_READ_BUF (32 * 1024)
#define HTTP_TLS_CHUNK (16 * 1024)
#define HTTP_MAX_REDIRECTS 32
#define HTTP_DEFAULT_TIMEOUT_MS 30000
#define HTTP_UD_META "tuipro.http.request"

typedef enum {
  HP_HEADERS = 0,
  HP_BODY_LENGTH,
  HP_BODY_EOF,
  HP_BODY_CHUNK_SIZE,
  HP_BODY_CHUNK_DATA,
  HP_BODY_CHUNK_CRLF,
  HP_BODY_CHUNK_LF,
  HP_BODY_CHUNK_TRAILER,
  HP_DONE,
} http_parse_state;

typedef enum {
  BODY_NONE = 0,
  BODY_LENGTH,
  BODY_EOF,
  BODY_CHUNKED,
} http_body_mode;

typedef enum {
  REDIRECT_FOLLOW = 0,
  REDIRECT_MANUAL,
  REDIRECT_ERROR,
} http_redirect_mode;

typedef enum {
  ENCODING_NONE = 0,
  ENCODING_GZIP,
  ENCODING_DEFLATE,
} http_encoding;

typedef struct {
  char *name;
  char *value;
} http_header;

typedef struct tp_http_request {
  struct tp_http_request *next;

  lua_State *L;
  int cb_ref;
  int ud_ref;
  struct tp_http_request **ud;

  bool closing;
  bool finished;
  bool silent;

  /* request description */
  bool https;
  char *host;
  char *host_header;
  bool host_is_ipv6;
  int port;
  char *path;
  char *current_url;
  char *method;
  char *body;
  size_t body_len;
  bool body_present;
  bool body_dropped;
  char *userinfo;
  char *request_data;
  size_t request_len;
  http_header *headers;
  size_t nheaders;
  size_t headers_cap;
  bool has_host;
  bool has_user_agent;
  bool has_accept;
  bool has_accept_encoding;
  bool has_connection;
  bool has_content_length;
  bool has_content_type;
  bool has_authorization;
  http_redirect_mode redirect_mode;
  int max_redirects;
  int redirects;
  bool redirected;
  bool reject_unauthorized;
  bool decompress;
  long timeout_ms;
  double start_ms;

  /* redirect restart */
  char *restart_url;
  bool restart_pending;

  /* libuv handles and requests */
  uv_getaddrinfo_t resolver;
  bool resolver_pending;
  struct addrinfo *addr_list;
  struct addrinfo *addr_next;
  int last_connect_error;
  bool connect_retry;
  struct sockaddr_storage peer;
  socklen_t peer_len;
  uv_tcp_t tcp;
  bool tcp_initialized;
  bool tcp_closing;
  bool tcp_read_started;
  uv_connect_t connect_req;
  bool connect_pending;
  uv_write_t write_req;
  bool write_pending;
  bool write_owned;
  char *write_data;
  uv_timer_t timer;
  bool timer_initialized;
  bool timer_closing;

  /* TLS */
  SSL *ssl;
  BIO *ssl_bio;
  BIO *net_bio;
  bool tls_ready;
  bool tls_socket_eof;
  size_t tls_sent;

  /* input */
  char read_buf[HTTP_READ_BUF];

  /* response parser */
  http_parse_state pstate;
  http_body_mode bmode;
  bool got_headers;
  int status;
  char status_text[128];
  char *hdr_buf;
  size_t hdr_len;
  size_t hdr_cap;
  size_t body_remaining;
  char *line_buf;
  size_t line_len;
  size_t line_cap;
  size_t chunk_remaining;
  char *location;
  http_encoding encoding;
  bool zlib_ready;
#ifdef TUIPRO_HAVE_ZLIB
  z_stream zs;
#endif
} tp_http_request;

typedef struct {
  bool https;
  char *host;
  char *host_header;
  bool host_is_ipv6;
  int port;
  char *path;
  char *url;
  char *userinfo;
} tp_url;

/* A libuv poll handle for a terminal fd.  Allocated on the heap because the
 * fd can change between terminal sessions and a uv_poll_t cannot be reused
 * until its close callback has run. */
typedef struct {
  uv_poll_t handle;
  int fd;
  bool active;
  bool is_stdin;
} tp_wait_watch;

/* ------------------------------------------------------------------------- */
/* Globals                                                                   */
/* ------------------------------------------------------------------------- */

static uv_loop_t g_loop_storage;
static uv_loop_t *g_loop = NULL;
static bool g_loop_ready = false;
static bool g_running = false;

static uv_timer_t g_wait_timer;
static bool g_wait_timer_ready = false;
static tp_wait_watch *g_stdin_watch = NULL;
static tp_wait_watch *g_signal_watch = NULL;
static bool g_stdin_ready = false;
static bool g_signal_ready = false;

static tp_http_request *g_requests = NULL;
static int g_active = 0;
static uint64_t g_seq = 0;

static SSL_CTX *g_ssl_ctx = NULL;

static void req_maybe_free(tp_http_request *req);
static void req_try_restart(tp_http_request *req);
static void tls_pump(tp_http_request *req);
static void http_feed_headers(tp_http_request *req, const char *data,
                              size_t len);
static void http_feed(tp_http_request *req, const char *data, size_t len);
static void req_eof(tp_http_request *req);
static void req_start_read(tp_http_request *req);
static void on_write(uv_write_t *write, int status);
static void on_req_timeout(uv_timer_t *timer);
static void on_tcp_close(uv_handle_t *h);
static void on_resolve(uv_getaddrinfo_t *resolver, int status,
                       struct addrinfo *res);
static void on_connect(uv_connect_t *connect, int status);
static void req_start_dns(tp_http_request *req);
static void req_try_connect(tp_http_request *req);

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} tp_buf;

static bool buf_reserve(tp_buf *b, size_t extra)
{
  if (extra > SIZE_MAX - b->len - 1)
    return false;
  size_t need = b->len + extra + 1;
  if (need <= b->cap)
    return true;
  size_t cap = b->cap ? b->cap : 256;
  while (cap < need) {
    if (cap > SIZE_MAX / 2)
      return false;
    cap *= 2;
  }
  char *p = realloc(b->data, cap);
  if (!p)
    return false;
  b->data = p;
  b->cap = cap;
  return true;
}

static bool buf_append(tp_buf *b, const char *data, size_t len)
{
  if (!buf_reserve(b, len))
    return false;
  memcpy(b->data + b->len, data, len);
  b->len += len;
  b->data[b->len] = '\0';
  return true;
}

static bool buf_puts(tp_buf *b, const char *s)
{
  return buf_append(b, s, strlen(s));
}

static bool buf_printf(tp_buf *b, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n < 0) {
    va_end(ap2);
    return false;
  }
  if (!buf_reserve(b, (size_t)n)) {
    va_end(ap2);
    return false;
  }
  vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap2);
  va_end(ap2);
  b->len += (size_t)n;
  return true;
}

static void buf_free(tp_buf *b)
{
  free(b->data);
  b->data = NULL;
  b->len = b->cap = 0;
}

static char *xstrdup(const char *s)
{
  if (!s)
    return NULL;
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p)
    memcpy(p, s, n);
  return p;
}

static void str_replace(char **dst, char *value)
{
  free(*dst);
  *dst = value;
}

static bool contains_ci(const char *haystack, const char *needle)
{
  if (!haystack || !needle)
    return false;
  size_t n = strlen(needle);
  for (const char *p = haystack; *p; p++) {
    if (strncasecmp(p, needle, n) == 0)
      return true;
  }
  return false;
}

static char *trim_dup(const char *s, size_t len)
{
  while (len > 0 && isspace((unsigned char)*s)) {
    s++;
    len--;
  }
  while (len > 0 && isspace((unsigned char)s[len - 1]))
    len--;
  char *p = malloc(len + 1);
  if (!p)
    return NULL;
  memcpy(p, s, len);
  p[len] = '\0';
  return p;
}

static const char *scheme_name(bool https)
{
  return https ? "https" : "http";
}

static int default_port(bool https)
{
  return https ? 443 : 80;
}

/* ------------------------------------------------------------------------- */
/* URL parsing                                                               */
/* ------------------------------------------------------------------------- */

static void url_free(tp_url *u)
{
  free(u->host);
  free(u->host_header);
  free(u->path);
  free(u->url);
  free(u->userinfo);
  memset(u, 0, sizeof(*u));
}

static char *normalize_path(const char *path)
{
  const char *query = strchr(path, '?');
  size_t plen = query ? (size_t)(query - path) : strlen(path);
  size_t qlen = query ? strlen(query) : 0;
  char *out = malloc(plen + qlen + 4);
  if (!out)
    return NULL;

  size_t o = 0;
  out[o++] = '/';
  size_t i = 0;
  if (i < plen && path[i] == '/')
    i++;
  while (i < plen) {
    size_t j = i;
    while (j < plen && path[j] != '/')
      j++;
    size_t seglen = j - i;
    if (seglen == 0 || (seglen == 1 && path[i] == '.')) {
      /* skip */
    } else if (seglen == 2 && path[i] == '.' && path[i + 1] == '.') {
      if (o > 1) {
        o--;
        while (o > 0 && out[o - 1] != '/')
          o--;
      }
    } else {
      if (o > 1)
        out[o++] = '/';
      memcpy(out + o, path + i, seglen);
      o += seglen;
    }
    i = j + 1;
  }
  if (plen > 0 && path[plen - 1] == '/' && o > 1)
    out[o++] = '/';
  if (o == 0)
    out[o++] = '/';
  if (qlen > 0) {
    memcpy(out + o, query, qlen);
    o += qlen;
  }
  out[o] = '\0';
  return out;
}

static int parse_url(const char *input, tp_url *out, char *err, size_t errlen)
{
  memset(out, 0, sizeof(*out));
  while (*input && isspace((unsigned char)*input))
    input++;
  size_t inlen = strlen(input);
  while (inlen > 0 && isspace((unsigned char)input[inlen - 1]))
    inlen--;
  if (inlen == 0 || inlen > 16384) {
    snprintf(err, errlen, "invalid URL");
    return -1;
  }

  const char *scheme_end = strstr(input, "://");
  if (!scheme_end || (size_t)(scheme_end - input) > 8) {
    snprintf(err, errlen, "invalid URL scheme");
    return -1;
  }
  size_t slen = (size_t)(scheme_end - input);
  if (slen == 4 && strncasecmp(input, "http", 4) == 0)
    out->https = false;
  else if (slen == 5 && strncasecmp(input, "https", 5) == 0)
    out->https = true;
  else {
    snprintf(err, errlen, "unsupported URL scheme '%.*s'", (int)slen, input);
    return -1;
  }

  const char *auth = scheme_end + 3;
  const char *auth_end = auth;
  while (*auth_end && *auth_end != '/' && *auth_end != '?' && *auth_end != '#')
    auth_end++;
  if (auth_end == auth) {
    snprintf(err, errlen, "URL has no host");
    return -1;
  }

  const char *host_start = auth;
  const char *host_end = auth_end;
  const char *at = NULL;
  for (const char *p = auth; p < auth_end; p++) {
    if (*p == '@')
      at = p;
  }
  if (at) {
    out->userinfo = trim_dup(auth, (size_t)(at - auth));
    if (!out->userinfo) {
      snprintf(err, errlen, "out of memory");
      goto fail;
    }
    host_start = at + 1;
  }
  if (host_start >= host_end) {
    snprintf(err, errlen, "URL has no host");
    goto fail;
  }

  const char *port_start = NULL;
  const char *port_end = NULL;
  if (*host_start == '[') {
    const char *close = memchr(host_start, ']', (size_t)(host_end - host_start));
    if (!close) {
      snprintf(err, errlen, "invalid IPv6 host");
      goto fail;
    }
    out->host = trim_dup(host_start + 1, (size_t)(close - host_start - 1));
    out->host_is_ipv6 = true;
    if (close + 1 < host_end) {
      if (close[1] != ':') {
        snprintf(err, errlen, "invalid port");
        goto fail;
      }
      port_start = close + 2;
      port_end = host_end;
    }
  } else {
    const char *colon = NULL;
    for (const char *p = host_start; p < host_end; p++) {
      if (*p == ':')
        colon = p;
    }
    if (colon) {
      port_start = colon + 1;
      port_end = host_end;
      host_end = colon;
    }
    out->host = trim_dup(host_start, (size_t)(host_end - host_start));
  }
  if (!out->host || !*out->host) {
    snprintf(err, errlen, "URL has no host");
    goto fail;
  }
  for (const char *p = out->host; *p; p++) {
    if (isspace((unsigned char)*p) || (unsigned char)*p < 32) {
      snprintf(err, errlen, "invalid host");
      goto fail;
    }
  }

  out->port = default_port(out->https);
  if (port_start && port_end > port_start) {
    long port = 0;
    for (const char *p = port_start; p < port_end; p++) {
      if (!isdigit((unsigned char)*p)) {
        snprintf(err, errlen, "invalid port");
        goto fail;
      }
      port = port * 10 + (*p - '0');
      if (port > 65535)
        break;
    }
    if (port < 1 || port > 65535) {
      snprintf(err, errlen, "invalid port");
      goto fail;
    }
    out->port = (int)port;
  }

  const char *path_start = auth_end;
  const char *frag = NULL;
  for (const char *p = auth_end; *p; p++) {
    if (*p == '#') {
      frag = p;
      break;
    }
  }
  const char *path_end = frag ? frag : input + inlen;
  char *raw_path;
  if (path_start >= path_end) {
    raw_path = xstrdup("/");
  } else if (*path_start == '?') {
    size_t n = (size_t)(path_end - path_start);
    raw_path = malloc(n + 2);
    if (raw_path) {
      raw_path[0] = '/';
      memcpy(raw_path + 1, path_start, n);
      raw_path[n + 1] = '\0';
    }
  } else {
    raw_path = trim_dup(path_start, (size_t)(path_end - path_start));
  }
  if (!raw_path) {
    snprintf(err, errlen, "out of memory");
    goto fail;
  }
  out->path = normalize_path(raw_path);
  free(raw_path);
  if (!out->path) {
    snprintf(err, errlen, "out of memory");
    goto fail;
  }

  /* Host header: brackets for IPv6, port only when non-default. */
  size_t host_len = strlen(out->host);
  bool default_p = out->port == default_port(out->https);
  size_t hlen = host_len + (out->host_is_ipv6 ? 2 : 0) + (default_p ? 0 : 12) + 1;
  out->host_header = malloc(hlen);
  if (!out->host_header) {
    snprintf(err, errlen, "out of memory");
    goto fail;
  }
  if (out->host_is_ipv6)
    snprintf(out->host_header, hlen, "[%s]%s", out->host,
             default_p ? "" : "");
  else
    snprintf(out->host_header, hlen, "%s", out->host);
  if (!default_p) {
    size_t used = strlen(out->host_header);
    snprintf(out->host_header + used, hlen - used, ":%d", out->port);
  }

  size_t ulen = strlen(scheme_name(out->https)) + 3 + strlen(out->host_header) +
                strlen(out->path) + 1;
  out->url = malloc(ulen);
  if (!out->url) {
    snprintf(err, errlen, "out of memory");
    goto fail;
  }
  snprintf(out->url, ulen, "%s://%s%s", scheme_name(out->https),
           out->host_header, out->path);
  return 0;

fail:
  url_free(out);
  return -1;
}

static char *resolve_location(const tp_http_request *req, const char *location)
{
  while (*location && isspace((unsigned char)*location))
    location++;
  size_t len = strlen(location);
  while (len > 0 && isspace((unsigned char)location[len - 1]))
    len--;
  if (len == 0)
    return xstrdup(req->current_url);
  char *loc = malloc(len + 1);
  if (!loc)
    return NULL;
  memcpy(loc, location, len);
  loc[len] = '\0';

  const char *scheme = scheme_name(req->https);
  char *result = NULL;
  if (strncasecmp(loc, "http://", 7) == 0 ||
      strncasecmp(loc, "https://", 8) == 0) {
    result = xstrdup(loc);
  } else if (loc[0] == '/' && loc[1] == '/') {
    if (asprintf(&result, "%s:%s", scheme, loc) < 0)
      result = NULL;
  } else if (loc[0] == '/') {
    if (asprintf(&result, "%s://%s%s", scheme, req->host_header, loc) < 0)
      result = NULL;
  } else if (loc[0] == '?') {
    const char *q = strchr(req->path, '?');
    size_t plen = q ? (size_t)(q - req->path) : strlen(req->path);
    if (asprintf(&result, "%s://%s%.*s%s", scheme, req->host_header,
                 (int)plen, req->path, loc) < 0)
      result = NULL;
  } else if (loc[0] == '#') {
    result = xstrdup(req->current_url);
  } else {
    const char *q = strchr(req->path, '?');
    size_t plen = q ? (size_t)(q - req->path) : strlen(req->path);
    const char *slash = NULL;
    for (size_t i = 0; i < plen; i++) {
      if (req->path[i] == '/')
        slash = req->path + i;
    }
    size_t dir_len = slash ? (size_t)(slash - req->path) + 1 : 1;
    if (asprintf(&result, "%s://%s%.*s%s", scheme, req->host_header,
                 (int)dir_len, req->path, loc) < 0)
      result = NULL;
  }
  free(loc);
  return result;
}

/* ------------------------------------------------------------------------- */
/* Request headers and body                                                  */
/* ------------------------------------------------------------------------- */

static void headers_clear(tp_http_request *req)
{
  for (size_t i = 0; i < req->nheaders; i++) {
    free(req->headers[i].name);
    free(req->headers[i].value);
  }
  free(req->headers);
  req->headers = NULL;
  req->nheaders = 0;
  req->headers_cap = 0;
}

static bool headers_push(tp_http_request *req, char *name, char *value)
{
  if (req->nheaders == req->headers_cap) {
    size_t cap = req->headers_cap ? req->headers_cap * 2 : 8;
    http_header *p = realloc(req->headers, cap * sizeof(*p));
    if (!p)
      return false;
    req->headers = p;
    req->headers_cap = cap;
  }
  req->headers[req->nheaders].name = name;
  req->headers[req->nheaders].value = value;
  req->nheaders++;
  return true;
}

static bool header_name_valid(const char *s)
{
  if (!s || !*s)
    return false;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (isalnum(*p))
      continue;
    switch (*p) {
    case '!': case '#': case '$': case '%': case '&': case '\'':
    case '*': case '+': case '-': case '.': case '^': case '_':
    case '`': case '|': case '~':
      break;
    default:
      return false;
    }
  }
  return true;
}

static bool header_value_valid(const char *s)
{
  if (!s)
    return false;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '\r' || *p == '\n' || *p == '\0')
      return false;
  }
  return true;
}

static bool header_named(const http_header *h, const char *name)
{
  return strcasecmp(h->name, name) == 0;
}

static bool req_has_header(const tp_http_request *req, const char *name)
{
  for (size_t i = 0; i < req->nheaders; i++) {
    if (header_named(&req->headers[i], name))
      return true;
  }
  return false;
}

static void req_remove_header(tp_http_request *req, const char *name)
{
  size_t out = 0;
  for (size_t i = 0; i < req->nheaders; i++) {
    if (header_named(&req->headers[i], name)) {
      free(req->headers[i].name);
      free(req->headers[i].value);
    } else {
      if (out != i)
        req->headers[out] = req->headers[i];
      out++;
    }
  }
  req->nheaders = out;
}

static void req_refresh_header_flags(tp_http_request *req)
{
  req->has_host = req_has_header(req, "Host");
  req->has_user_agent = req_has_header(req, "User-Agent");
  req->has_accept = req_has_header(req, "Accept");
  req->has_accept_encoding = req_has_header(req, "Accept-Encoding");
  req->has_connection = req_has_header(req, "Connection");
  req->has_content_length = req_has_header(req, "Content-Length");
  req->has_content_type = req_has_header(req, "Content-Type");
  req->has_authorization = req_has_header(req, "Authorization");
}

static char *base64_encode(const unsigned char *data, size_t len)
{
  static const char b64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t out_len = ((len + 2) / 3) * 4;
  char *out = malloc(out_len + 1);
  if (!out)
    return NULL;
  size_t i, j = 0;
  for (i = 0; i + 2 < len; i += 3) {
    unsigned int n = (unsigned int)data[i] << 16 |
                     (unsigned int)data[i + 1] << 8 |
                     (unsigned int)data[i + 2];
    out[j++] = b64[(n >> 18) & 63];
    out[j++] = b64[(n >> 12) & 63];
    out[j++] = b64[(n >> 6) & 63];
    out[j++] = b64[n & 63];
  }
  if (i < len) {
    unsigned int n = (unsigned int)data[i] << 16;
    out[j++] = b64[(n >> 18) & 63];
    if (i + 1 < len) {
      n |= (unsigned int)data[i + 1] << 8;
      out[j++] = b64[(n >> 12) & 63];
      out[j++] = b64[(n >> 6) & 63];
      out[j++] = '=';
    } else {
      out[j++] = b64[(n >> 12) & 63];
      out[j++] = '=';
      out[j++] = '=';
    }
  }
  out[j] = '\0';
  return out;
}

static bool build_request(tp_http_request *req)
{
  free(req->request_data);
  req->request_data = NULL;
  req->request_len = 0;

  tp_buf b = { 0 };
  if (!buf_printf(&b, "%s %s HTTP/1.1\r\n", req->method, req->path))
    goto oom;
  if (!req->has_host && !buf_printf(&b, "Host: %s\r\n", req->host_header))
    goto oom;
  if (!req->has_user_agent &&
      !buf_puts(&b, "User-Agent: tuipro/" TUIPRO_VERSION_STRING "\r\n"))
    goto oom;
  if (!req->has_accept && !buf_puts(&b, "Accept: */*\r\n"))
    goto oom;
#ifdef TUIPRO_HAVE_ZLIB
  if (req->decompress && !req->has_accept_encoding &&
      !buf_puts(&b, "Accept-Encoding: gzip, deflate\r\n"))
    goto oom;
#endif
  if (!req->has_connection && !buf_puts(&b, "Connection: close\r\n"))
    goto oom;

  if (!req->has_authorization && req->userinfo) {
    size_t ulen = strlen(req->userinfo);
    bool has_colon = strchr(req->userinfo, ':') != NULL;
    char *creds = malloc(ulen + 2);
    if (!creds)
      goto oom;
    memcpy(creds, req->userinfo, ulen);
    if (!has_colon) {
      creds[ulen] = ':';
      creds[ulen + 1] = '\0';
    } else {
      creds[ulen] = '\0';
    }
    char *enc = base64_encode((const unsigned char *)creds, strlen(creds));
    free(creds);
    if (!enc)
      goto oom;
    bool ok = buf_printf(&b, "Authorization: Basic %s\r\n", enc);
    free(enc);
    if (!ok)
      goto oom;
  }

  for (size_t i = 0; i < req->nheaders; i++) {
    http_header *h = &req->headers[i];
    if (req->body_dropped &&
        (strcasecmp(h->name, "Content-Length") == 0 ||
         strcasecmp(h->name, "Transfer-Encoding") == 0))
      continue;
    if (!buf_printf(&b, "%s: %s\r\n", h->name, h->value))
      goto oom;
  }

  if (req->body_present && !req->body_dropped) {
    if (!req->has_content_type && !buf_puts(&b, "Content-Type: text/plain;charset=UTF-8\r\n"))
      goto oom;
    if (!req->has_content_length &&
        !buf_printf(&b, "Content-Length: %zu\r\n", req->body_len))
      goto oom;
  }
  if (!buf_puts(&b, "\r\n"))
    goto oom;
  if (req->body_present && !req->body_dropped && req->body_len > 0) {
    if (!buf_append(&b, req->body, req->body_len))
      goto oom;
  }

  if (b.len > UINT_MAX) {
    buf_free(&b);
    return false;
  }
  req->request_data = b.data;
  req->request_len = b.len;
  return true;

oom:
  buf_free(&b);
  return false;
}

/* ------------------------------------------------------------------------- */
/* Lua callback helpers                                                      */
/* ------------------------------------------------------------------------- */

static bool call_cb(tp_http_request *req, const char *name, int nargs)
{
  lua_State *L = req->L;
  if (!L)
    return false;
  if (req->silent || req->cb_ref == LUA_NOREF) {
    lua_pop(L, nargs);
    return true;
  }
  /* Insert the callback function just below the arguments so lua_pcall sees
   * the usual function-then-arguments layout. */
  int base = lua_gettop(L) - nargs + 1;
  lua_rawgeti(L, LUA_REGISTRYINDEX, req->cb_ref);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1 + nargs);
    return true;
  }
  lua_getfield(L, -1, name);
  lua_remove(L, -2);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1 + nargs);
    return true;
  }
  if (nargs > 0)
    lua_insert(L, base);
  if (lua_pcall(L, nargs, 0, 0) != 0) {
    const char *err = lua_tostring(L, -1);
    fprintf(stderr, "[tuipro] fetch callback %s failed: %s\n", name,
            err ? err : "unknown error");
    lua_pop(L, 1);
    req->finished = true;
    req->closing = true;
    return false;
  }
  return true;
}

static void req_close(tp_http_request *req);

static void req_fail(tp_http_request *req, const char *code, const char *fmt, ...)
{
  if (req->finished)
    return;
  req->finished = true;
  if (!req->silent && req->L && req->cb_ref != LUA_NOREF) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    lua_pushstring(req->L, msg);
    lua_pushstring(req->L, code ? code : "TypeError");
    if (!call_cb(req, "on_error", 2))
      req->silent = true;
  }
  req_close(req);
}

/* ------------------------------------------------------------------------- */
/* TLS                                                                       */
/* ------------------------------------------------------------------------- */

static SSL_CTX *ssl_ctx_get(void)
{
  if (g_ssl_ctx)
    return g_ssl_ctx;
  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
  if (!ctx)
    return NULL;
  SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
  SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);
  SSL_CTX_set_default_verify_paths(ctx);
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
  static const unsigned char alpn[] = { 8, 'h', 't', 't', 'p', '/', '1', '.', '1' };
  SSL_CTX_set_alpn_protos(ctx, alpn, sizeof(alpn));
  g_ssl_ctx = ctx;
  return ctx;
}

static void tls_cleanup(tp_http_request *req)
{
  if (req->ssl) {
    SSL_free(req->ssl);
    req->ssl = NULL;
    req->ssl_bio = NULL;
  }
  if (req->net_bio) {
    BIO_free(req->net_bio);
    req->net_bio = NULL;
  }
#ifdef TUIPRO_HAVE_ZLIB
  if (req->zlib_ready) {
    inflateEnd(&req->zs);
    req->zlib_ready = false;
  }
#endif
}

static void tls_error(tp_http_request *req, const char *what)
{
  unsigned long e = ERR_peek_last_error();
  char buf[256];
  if (e)
    ERR_error_string_n(e, buf, sizeof(buf));
  else
    snprintf(buf, sizeof(buf), "%s", what);
  ERR_clear_error();
  req_fail(req, "TypeError", "TLS %s: %s", what, buf);
}

static bool is_ip_address(const char *host)
{
  unsigned char buf[16];
  return inet_pton(AF_INET, host, buf) == 1 ||
         inet_pton(AF_INET6, host, buf) == 1;
}

static void tls_flush(tp_http_request *req);

static void tls_want_read(tp_http_request *req)
{
  if (req->tls_socket_eof) {
    req_eof(req);
    return;
  }
  req_start_read(req);
}

static void tls_flush(tp_http_request *req)
{
  if (req->closing || req->finished || !req->net_bio)
    return;
  if (req->write_pending)
    return;
  int pending = BIO_ctrl_pending(req->net_bio);
  if (pending <= 0)
    return;
  size_t n = (size_t)pending;
  if (n > 64 * 1024)
    n = 64 * 1024;
  char *buf = malloc(n);
  if (!buf) {
    req_fail(req, "TypeError", "out of memory");
    return;
  }
  int got = BIO_read(req->net_bio, buf, (int)n);
  if (got <= 0) {
    free(buf);
    return;
  }
  req->write_req.data = req;
  uv_buf_t b = uv_buf_init(buf, (unsigned int)got);
  int rc = uv_write(&req->write_req, (uv_stream_t *)&req->tcp, &b, 1,
                    on_write);
  if (rc < 0) {
    free(buf);
    req_fail(req, "TypeError", "write failed: %s", uv_strerror(rc));
    return;
  }
  req->write_data = buf;
  req->write_owned = true;
  req->write_pending = true;
}

static void tls_pump(tp_http_request *req)
{
  if (req->closing || req->finished || !req->ssl)
    return;

  if (!req->tls_ready) {
    ERR_clear_error();
    int rc = SSL_do_handshake(req->ssl);
    if (rc != 1) {
      int e = SSL_get_error(req->ssl, rc);
      if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
        tls_flush(req);
        if (e == SSL_ERROR_WANT_READ)
          tls_want_read(req);
        return;
      }
      tls_error(req, "handshake failed");
      return;
    }
    req->tls_ready = true;
  }

  if (req->tls_sent < req->request_len) {
    while (req->tls_sent < req->request_len) {
      size_t remain = req->request_len - req->tls_sent;
      int chunk = remain > INT_MAX ? INT_MAX : (int)remain;
      ERR_clear_error();
      int n = SSL_write(req->ssl, req->request_data + req->tls_sent, chunk);
      if (n > 0) {
        req->tls_sent += (size_t)n;
        continue;
      }
      int e = SSL_get_error(req->ssl, n);
      if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
        tls_flush(req);
        if (e == SSL_ERROR_WANT_READ)
          tls_want_read(req);
        return;
      }
      tls_error(req, "write failed");
      return;
    }
  }

  for (;;) {
    char buf[HTTP_TLS_CHUNK];
    ERR_clear_error();
    int n = SSL_read(req->ssl, buf, sizeof(buf));
    if (n > 0) {
      http_feed(req, buf, (size_t)n);
      if (req->finished || req->closing)
        return;
      continue;
    }
    int e = SSL_get_error(req->ssl, n);
    if (e == SSL_ERROR_WANT_READ) {
      tls_flush(req);
      tls_want_read(req);
      return;
    }
    if (e == SSL_ERROR_WANT_WRITE) {
      tls_flush(req);
      return;
    }
    if (e == SSL_ERROR_ZERO_RETURN) {
      req_eof(req);
      return;
    }
    if (e == SSL_ERROR_SYSCALL && ERR_peek_last_error() == 0) {
      req_eof(req);
      return;
    }
    tls_error(req, "read failed");
    return;
  }
}

/* ------------------------------------------------------------------------- */
/* Response parsing                                                          */
/* ------------------------------------------------------------------------- */

static void zlib_reset(tp_http_request *req)
{
#ifdef TUIPRO_HAVE_ZLIB
  if (req->zlib_ready) {
    inflateEnd(&req->zs);
    req->zlib_ready = false;
  }
#endif
  req->encoding = ENCODING_NONE;
}

static bool zlib_start(tp_http_request *req)
{
#ifdef TUIPRO_HAVE_ZLIB
  if (req->encoding == ENCODING_NONE || !req->decompress)
    return true;
  memset(&req->zs, 0, sizeof(req->zs));
  int window = req->encoding == ENCODING_GZIP ? 16 + MAX_WBITS : MAX_WBITS;
  if (inflateInit2(&req->zs, window) != Z_OK) {
    req_fail(req, "TypeError", "cannot initialize decompressor");
    return false;
  }
  req->zlib_ready = true;
  return true;
#else
  (void)req;
  return true;
#endif
}

static void deliver_raw(tp_http_request *req, const char *data, size_t len)
{
  if (len == 0 || req->finished || req->closing || !req->L)
    return;
  lua_pushlstring(req->L, data, len);
  if (!call_cb(req, "on_data", 1)) {
    req->silent = true;
    req_close(req);
  }
}

static void deliver_body(tp_http_request *req, const char *data, size_t len)
{
  if (len == 0 || req->finished || req->closing)
    return;
#ifdef TUIPRO_HAVE_ZLIB
  if (req->zlib_ready) {
    size_t offset = 0;
    while (offset < len && !req->finished) {
      uInt in = (uInt)((len - offset) > UINT_MAX ? UINT_MAX : (len - offset));
      req->zs.next_in = (Bytef *)(data + offset);
      req->zs.avail_in = in;
      while (req->zs.avail_in > 0 && !req->finished) {
        unsigned char out[HTTP_TLS_CHUNK];
        req->zs.next_out = out;
        req->zs.avail_out = sizeof(out);
        int rc = inflate(&req->zs, Z_NO_FLUSH);
        size_t produced = sizeof(out) - req->zs.avail_out;
        if (produced > 0)
          deliver_raw(req, (const char *)out, produced);
        if (rc == Z_STREAM_END) {
          inflateEnd(&req->zs);
          req->zlib_ready = false;
          break;
        }
        if (rc == Z_BUF_ERROR && produced == 0) {
          req_fail(req, "TypeError", "truncated compressed response");
          return;
        }
        if (rc != Z_OK && rc != Z_BUF_ERROR) {
          req_fail(req, "TypeError", "invalid compressed response");
          return;
        }
        if (produced == 0 && req->zs.avail_in == 0)
          break;
      }
      offset += in;
      if (req->zs.avail_in == 0)
        continue;
    }
    return;
  }
#endif
  deliver_raw(req, data, len);
}

static void req_finish_success(tp_http_request *req)
{
  if (req->finished)
    return;
  req->finished = true;
#ifdef TUIPRO_HAVE_ZLIB
  if (req->zlib_ready) {
    inflateEnd(&req->zs);
    req->zlib_ready = false;
  }
#endif
  if (!req->silent && req->L && req->cb_ref != LUA_NOREF) {
    if (!call_cb(req, "on_end", 0))
      req->silent = true;
  }
  req_close(req);
}

static void req_eof(tp_http_request *req)
{
  if (req->finished || req->closing)
    return;
  if (!req->got_headers) {
    req_fail(req, "TypeError", "connection closed before response headers");
    return;
  }
  if (req->bmode == BODY_EOF) {
    req_finish_success(req);
    return;
  }
  req_fail(req, "TypeError", "connection closed before response completed");
}

static void process_chunked(tp_http_request *req, const char *data, size_t len)
{
  size_t i = 0;
  while (i < len && !req->finished && !req->closing) {
    switch (req->pstate) {
    case HP_BODY_CHUNK_SIZE:
    case HP_BODY_CHUNK_TRAILER: {
      bool got_line = false;
      while (i < len) {
        char c = data[i++];
        if (c == '\n') {
          if (req->line_len > 0 && req->line_buf[req->line_len - 1] == '\r')
            req->line_len--;
          req->line_buf[req->line_len] = '\0';
          if (req->pstate == HP_BODY_CHUNK_SIZE) {
            char *start = req->line_buf;
            while (*start == ' ' || *start == '\t')
              start++;
            errno = 0;
            char *endp = NULL;
            unsigned long long size = strtoull(start, &endp, 16);
            if (endp == start || errno == ERANGE) {
              req_fail(req, "TypeError", "invalid chunk size");
              return;
            }
            /* Allow optional whitespace and chunk extensions, but reject
             * arbitrary trailing garbage. */
            while (*endp == ' ' || *endp == '\t')
              endp++;
            if (*endp != '\0' && *endp != ';') {
              req_fail(req, "TypeError", "invalid chunk size");
              return;
            }
            req->line_len = 0;
            if (size == 0) {
              req->pstate = HP_BODY_CHUNK_TRAILER;
            } else {
              if (size > (unsigned long long)SIZE_MAX) {
                req_fail(req, "TypeError", "chunk too large");
                return;
              }
              req->chunk_remaining = (size_t)size;
              req->pstate = HP_BODY_CHUNK_DATA;
            }
          } else {
            if (req->line_len == 0) {
              req->pstate = HP_DONE;
              req_finish_success(req);
              return;
            }
            req->line_len = 0;
          }
          got_line = true;
          break;
        }
        if (req->line_len >= HTTP_MAX_LINE) {
          req_fail(req, "TypeError", "chunk line too long");
          return;
        }
        if (req->line_len + 1 >= req->line_cap) {
          size_t cap = req->line_cap ? req->line_cap * 2 : 128;
          char *p = realloc(req->line_buf, cap);
          if (!p) {
            req_fail(req, "TypeError", "out of memory");
            return;
          }
          req->line_buf = p;
          req->line_cap = cap;
        }
        req->line_buf[req->line_len++] = c;
      }
      if (!got_line)
        return;
      break;
    }
    case HP_BODY_CHUNK_DATA: {
      size_t take = len - i;
      if (take > req->chunk_remaining)
        take = req->chunk_remaining;
      deliver_body(req, data + i, take);
      i += take;
      req->chunk_remaining -= take;
      if (req->chunk_remaining == 0)
        req->pstate = HP_BODY_CHUNK_CRLF;
      break;
    }
    case HP_BODY_CHUNK_CRLF:
      if (data[i] == '\r') {
        i++;
        req->pstate = HP_BODY_CHUNK_LF;
      } else if (data[i] == '\n') {
        i++;
        req->pstate = HP_BODY_CHUNK_SIZE;
      } else {
        req_fail(req, "TypeError", "invalid chunk terminator");
        return;
      }
      break;
    case HP_BODY_CHUNK_LF:
      if (data[i] != '\n') {
        req_fail(req, "TypeError", "invalid chunk terminator");
        return;
      }
      i++;
      req->pstate = HP_BODY_CHUNK_SIZE;
      break;
    default:
      return;
    }
  }
}

static void process_body(tp_http_request *req, const char *data, size_t len)
{
  if (len == 0 || req->finished || req->closing)
    return;
  switch (req->bmode) {
  case BODY_NONE:
    return;
  case BODY_LENGTH: {
    size_t take = len;
    if (take > req->body_remaining)
      take = req->body_remaining;
    deliver_body(req, data, take);
    req->body_remaining -= take;
    if (req->body_remaining == 0 && !req->finished)
      req_finish_success(req);
    return;
  }
  case BODY_EOF:
    deliver_body(req, data, len);
    return;
  case BODY_CHUNKED:
    process_chunked(req, data, len);
    return;
  }
}

static bool is_redirect_status(int status)
{
  return status == 301 || status == 302 || status == 303 || status == 307 ||
         status == 308;
}

static void req_set_method(tp_http_request *req, const char *method)
{
  char *m = xstrdup(method);
  if (!m)
    return;
  str_replace(&req->method, m);
  req_refresh_header_flags(req);
}

static void req_drop_body(tp_http_request *req)
{
  free(req->body);
  req->body = NULL;
  req->body_len = 0;
  req->body_present = false;
  req->body_dropped = true;
}

static void req_redirect(tp_http_request *req)
{
  char *resolved = resolve_location(req, req->location ? req->location : "");
  free(req->location);
  req->location = NULL;
  if (!resolved) {
    req_fail(req, "TypeError", "out of memory resolving redirect");
    return;
  }
  if (req->redirects >= req->max_redirects) {
    free(resolved);
    req_fail(req, "TypeError", "redirect count exceeded");
    return;
  }
  req->redirects++;
  req->redirected = true;

  if (req->status == 303 &&
      strcasecmp(req->method, "GET") != 0 &&
      strcasecmp(req->method, "HEAD") != 0) {
    req_set_method(req, "GET");
    req_drop_body(req);
  } else if ((req->status == 301 || req->status == 302) &&
             strcasecmp(req->method, "POST") == 0) {
    req_set_method(req, "GET");
    req_drop_body(req);
  }

  free(req->restart_url);
  req->restart_url = resolved;
  req->restart_pending = true;
  req->closing = false;

  /* Drop the response we are following and wait for the TCP handle to close
   * before starting the next request.  Resetting the parser here prevents
   * http_feed_headers() from re-parsing the same redirect response. */
  req->pstate = HP_DONE;
  req->hdr_len = 0;
  req->got_headers = false;

  if (req->tcp_read_started) {
    uv_read_stop((uv_stream_t *)&req->tcp);
    req->tcp_read_started = false;
  }
  if (req->tcp_initialized && !req->tcp_closing) {
    req->tcp_closing = true;
    uv_close((uv_handle_t *)&req->tcp, on_tcp_close);
  }
  tls_cleanup(req);
  req_try_restart(req);
}

static void parse_response_headers(tp_http_request *req, size_t header_len,
                                   size_t body_off)
{
  lua_State *L = req->L;
  char *p = req->hdr_buf;
  char *end = p + header_len;

  char *line = p;
  char *nl = memchr(line, '\n', (size_t)(end - line));
  char *line_end = nl ? nl : end;
  if (line_end > line && line_end[-1] == '\r')
    line_end[-1] = '\0';
  if (nl)
    *nl = '\0';
  p = nl ? nl + 1 : end;

  if (strncasecmp(line, "HTTP/", 5) != 0) {
    req_fail(req, "TypeError", "invalid HTTP status line");
    return;
  }
  char *sp = strchr(line, ' ');
  if (!sp || strncasecmp(line, "HTTP/1.", 7) != 0) {
    req_fail(req, "TypeError", "invalid HTTP status line");
    return;
  }
  char *code = sp + 1;
  if (!isdigit((unsigned char)code[0]) ||
      !isdigit((unsigned char)code[1]) ||
      !isdigit((unsigned char)code[2]) ||
      (code[3] != '\0' && code[3] != ' ')) {
    req_fail(req, "TypeError", "invalid HTTP status code");
    return;
  }
  int status = (code[0] - '0') * 100 + (code[1] - '0') * 10 + (code[2] - '0');
  if (status < 100) {
    req_fail(req, "TypeError", "invalid HTTP status code");
    return;
  }
  char *text = code + 3;
  while (*text == ' ')
    text++;

  req->status = status;
  snprintf(req->status_text, sizeof(req->status_text), "%s", text);
  free(req->location);
  req->location = NULL;
  req->encoding = ENCODING_NONE;
  bool chunked = false;
  bool have_length = false;
  unsigned long long content_length = 0;

  lua_createtable(L, 0, 16);
  int table_idx = lua_gettop(L);
  int n = 1;

  while (p < end) {
    line = p;
    nl = memchr(line, '\n', (size_t)(end - line));
    line_end = nl ? nl : end;
    if (line_end > line && line_end[-1] == '\r')
      line_end[-1] = '\0';
    if (nl)
      *nl = '\0';
    p = nl ? nl + 1 : end;
    if (*line == '\0')
      break;

    char *colon = strchr(line, ':');
    if (!colon)
      continue;
    *colon = '\0';
    char *name = line;
    char *value = colon + 1;
    while (*value == ' ' || *value == '\t')
      value++;
    char *v_end = value + strlen(value);
    while (v_end > value && (v_end[-1] == ' ' || v_end[-1] == '\t'))
      *--v_end = '\0';

    lua_pushstring(L, name);
    lua_rawseti(L, table_idx, n++);
    lua_pushstring(L, value);
    lua_rawseti(L, table_idx, n++);

    char lname[128];
    size_t ln = strlen(name);
    if (ln < sizeof(lname)) {
      for (size_t i = 0; i < ln; i++)
        lname[i] = (char)tolower((unsigned char)name[i]);
      lname[ln] = '\0';
      if (strcmp(lname, "content-length") == 0) {
        char *cp = value;
        while (*cp == ' ' || *cp == '\t')
          cp++;
        char *ep = NULL;
        errno = 0;
        unsigned long long v = strtoull(cp, &ep, 10);
        if (ep == cp || errno == ERANGE) {
          lua_pop(L, 1);
          req_fail(req, "TypeError", "invalid Content-Length");
          return;
        }
        while (*ep == ' ' || *ep == '\t')
          ep++;
        if (*ep != '\0' || (have_length && content_length != v)) {
          lua_pop(L, 1);
          req_fail(req, "TypeError", "invalid Content-Length");
          return;
        }
        content_length = v;
        have_length = true;
      } else if (strcmp(lname, "transfer-encoding") == 0) {
        if (contains_ci(value, "chunked"))
          chunked = true;
      } else if (strcmp(lname, "location") == 0) {
        free(req->location);
        req->location = xstrdup(value);
      } else if (strcmp(lname, "content-encoding") == 0) {
        if (strcasecmp(value, "gzip") == 0 ||
            strcasecmp(value, "x-gzip") == 0)
          req->encoding = ENCODING_GZIP;
        else if (strcasecmp(value, "deflate") == 0)
          req->encoding = ENCODING_DEFLATE;
      }
    }
  }

  if (status >= 100 && status < 200) {
    lua_pop(L, 1);
    if (status == 101) {
      req_fail(req, "TypeError", "unexpected protocol switch");
      return;
    }
    size_t left = req->hdr_len - body_off;
    if (left > 0)
      memmove(req->hdr_buf, req->hdr_buf + body_off, left);
    req->hdr_len = left;
    req->pstate = HP_HEADERS;
    req->got_headers = false;
    return;
  }

  if (req->redirect_mode != REDIRECT_MANUAL && is_redirect_status(status) &&
      req->location) {
    if (req->redirect_mode == REDIRECT_ERROR) {
      lua_pop(L, 1);
      req_fail(req, "TypeError", "redirect mode is error");
      return;
    }
    lua_pop(L, 1);
    req_redirect(req);
    return;
  }

  bool no_body = (strcasecmp(req->method, "HEAD") == 0) || status == 204 ||
                 status == 304;
  if (no_body)
    req->bmode = BODY_NONE;
  else if (chunked)
    req->bmode = BODY_CHUNKED;
  else if (have_length)
    req->bmode = BODY_LENGTH;
  else
    req->bmode = BODY_EOF;

  if (have_length && content_length > (unsigned long long)SIZE_MAX) {
    lua_pop(L, 1);
    req_fail(req, "TypeError", "response body too large");
    return;
  }
  req->body_remaining = have_length ? (size_t)content_length : 0;
  req->pstate = req->bmode == BODY_CHUNKED ? HP_BODY_CHUNK_SIZE
                : req->bmode == BODY_LENGTH ? HP_BODY_LENGTH
                : req->bmode == BODY_EOF ? HP_BODY_EOF
                                         : HP_DONE;
  req->got_headers = true;

  if (!zlib_start(req)) {
    lua_pop(L, 1);
    return;
  }

  lua_pushinteger(L, status);
  lua_pushstring(L, req->status_text);
  lua_pushvalue(L, table_idx);
  lua_pushstring(L, req->current_url ? req->current_url : "");
  lua_pushboolean(L, req->redirected ? 1 : 0);
  if (!call_cb(req, "on_headers", 5)) {
    lua_pop(L, 1);
    req->silent = true;
    req_close(req);
    return;
  }
  lua_remove(L, table_idx);
  if (req->finished || req->closing)
    return;

  if (req->bmode == BODY_NONE) {
    req_finish_success(req);
    return;
  }
  if (req->bmode == BODY_LENGTH && req->body_remaining == 0) {
    req_finish_success(req);
    return;
  }

  size_t left = req->hdr_len - body_off;
  if (left > 0) {
    process_body(req, req->hdr_buf + body_off, left);
    req->hdr_len = 0;
  }
}

static void http_feed(tp_http_request *req, const char *data, size_t len)
{
  if (req->finished || req->closing || len == 0)
    return;
  if (req->pstate == HP_HEADERS)
    http_feed_headers(req, data, len);
  else
    process_body(req, data, len);
}

static void http_feed_headers(tp_http_request *req, const char *data, size_t len)
{
  if (len > 0) {
    if (req->hdr_len + len > HTTP_MAX_HEADER + HTTP_READ_BUF) {
      req_fail(req, "TypeError", "response headers too large");
      return;
    }
    if (req->hdr_len + len + 1 > req->hdr_cap) {
      size_t cap = req->hdr_cap ? req->hdr_cap : 1024;
      while (cap < req->hdr_len + len + 1)
        cap *= 2;
      char *p = realloc(req->hdr_buf, cap);
      if (!p) {
        req_fail(req, "TypeError", "out of memory");
        return;
      }
      req->hdr_buf = p;
      req->hdr_cap = cap;
    }
    memcpy(req->hdr_buf + req->hdr_len, data, len);
    req->hdr_len += len;
    req->hdr_buf[req->hdr_len] = '\0';
  }

  for (;;) {
    size_t end = 0;
    for (size_t i = 0; i + 3 < req->hdr_len; i++) {
      if (req->hdr_buf[i] == '\r' && req->hdr_buf[i + 1] == '\n' &&
          req->hdr_buf[i + 2] == '\r' && req->hdr_buf[i + 3] == '\n') {
        end = i + 4;
        break;
      }
      if (req->hdr_buf[i] == '\n' && req->hdr_buf[i + 1] == '\n') {
        end = i + 2;
        break;
      }
    }
    if (end == 0) {
      if (req->hdr_len > HTTP_MAX_HEADER)
        req_fail(req, "TypeError", "response headers too large");
      return;
    }

    parse_response_headers(req, end, end);
    if (req->finished || req->closing)
      return;
    if (req->pstate == HP_HEADERS) {
      /* 1xx informational response: the parser moved the next response to
       * the front of the header buffer. */
      if (req->hdr_len > 0)
        continue;
      return;
    }
    req->hdr_len = 0;
    return;
  }
}

static void http_feed_data(tp_http_request *req, const char *data, size_t len)
{
  http_feed(req, data, len);
}

/* ------------------------------------------------------------------------- */
/* Request lifecycle                                                         */
/* ------------------------------------------------------------------------- */

static void req_unlink(tp_http_request *req)
{
  tp_http_request **pp = &g_requests;
  while (*pp) {
    if (*pp == req) {
      *pp = req->next;
      break;
    }
    pp = &(*pp)->next;
  }
  if (g_active > 0)
    g_active--;
  g_seq++;
}

static void req_free_addrs(tp_http_request *req)
{
  if (req->addr_list) {
    uv_freeaddrinfo(req->addr_list);
    req->addr_list = NULL;
  }
  req->addr_next = NULL;
}

static void req_free_now(tp_http_request *req)
{
  lua_State *L = req->L;
  if (req->ud) {
    if (*req->ud)
      *req->ud = NULL;
    req->ud = NULL;
  }
  if (L) {
    if (req->cb_ref != LUA_NOREF) {
      luaL_unref(L, LUA_REGISTRYINDEX, req->cb_ref);
      req->cb_ref = LUA_NOREF;
    }
    if (req->ud_ref != LUA_NOREF) {
      luaL_unref(L, LUA_REGISTRYINDEX, req->ud_ref);
      req->ud_ref = LUA_NOREF;
    }
  }
  tls_cleanup(req);
  req_free_addrs(req);
  free(req->host);
  free(req->host_header);
  free(req->path);
  free(req->current_url);
  free(req->method);
  free(req->body);
  free(req->userinfo);
  free(req->request_data);
  free(req->restart_url);
  free(req->hdr_buf);
  free(req->line_buf);
  free(req->location);
  free(req->write_data);
  headers_clear(req);
  free(req);
}

static void req_maybe_free(tp_http_request *req)
{
  if (!req->closing)
    return;
  if (req->resolver_pending || req->connect_pending || req->write_pending)
    return;
  if (req->tcp_initialized || req->timer_initialized)
    return;
  req_unlink(req);
  req_free_now(req);
}

static void on_timer_close(uv_handle_t *h)
{
  tp_http_request *req = (tp_http_request *)h->data;
  req->timer_initialized = false;
  req->timer_closing = false;
  req_maybe_free(req);
}

static void on_req_timeout(uv_timer_t *timer)
{
  tp_http_request *req = (tp_http_request *)timer->data;
  if (!req || req->finished || req->closing)
    return;
  uv_timer_stop(timer);
  req_fail(req, "TimeoutError", "request timed out after %ld ms",
           req->timeout_ms);
}

static void on_tcp_close(uv_handle_t *h)
{
  tp_http_request *req = (tp_http_request *)h->data;
  req->tcp_initialized = false;
  req->tcp_closing = false;
  req->tcp_read_started = false;
  if (!req->closing && req->connect_retry) {
    req->connect_retry = false;
    req_try_connect(req);
  } else if (!req->closing && req->restart_pending) {
    req_try_restart(req);
  } else {
    req_maybe_free(req);
  }
}

static void req_close(tp_http_request *req)
{
  if (req->closing) {
    req_maybe_free(req);
    return;
  }
  req->closing = true;

  if (req->resolver_pending) {
    /* The callback still runs with UV_ECANCELED after a successful cancel, so
     * resolver_pending must stay set until on_resolve() releases the request. */
    uv_cancel((uv_req_t *)&req->resolver);
  }
  if (req->timer_initialized && !req->timer_closing) {
    uv_timer_stop(&req->timer);
    req->timer_closing = true;
    uv_close((uv_handle_t *)&req->timer, on_timer_close);
  }
  if (req->tcp_initialized && !req->tcp_closing) {
    if (req->tcp_read_started) {
      uv_read_stop((uv_stream_t *)&req->tcp);
      req->tcp_read_started = false;
    }
    req->tcp_closing = true;
    uv_close((uv_handle_t *)&req->tcp, on_tcp_close);
  }
  tls_cleanup(req);
  req_maybe_free(req);
}

static void req_try_restart(tp_http_request *req)
{
  if (!req->restart_pending || req->closing || req->finished)
    return;
  if (req->resolver_pending || req->connect_pending || req->write_pending ||
      req->tcp_initialized)
    return;

  char *url = req->restart_url;
  req->restart_url = NULL;
  req->restart_pending = false;

  tp_url next;
  char err[256];
  if (parse_url(url, &next, err, sizeof(err)) < 0) {
    free(url);
    req_fail(req, "TypeError", "%s", err);
    return;
  }
  bool cross_origin = next.https != req->https || next.port != req->port ||
                      strcasecmp(next.host, req->host) != 0;
  if (cross_origin) {
    req_remove_header(req, "Authorization");
    req_remove_header(req, "Cookie");
    req_remove_header(req, "Proxy-Authorization");
    req_remove_header(req, "Host");
  }
  free(url);
  req_free_addrs(req);
  req->connect_retry = false;
  req->last_connect_error = 0;

  free(req->host);
  free(req->host_header);
  free(req->path);
  free(req->current_url);
  free(req->userinfo);
  req->https = next.https;
  req->host = next.host;
  req->host_header = next.host_header;
  req->host_is_ipv6 = next.host_is_ipv6;
  req->port = next.port;
  req->path = next.path;
  req->current_url = next.url;
  req->userinfo = next.userinfo;
  next.host = next.host_header = next.path = next.url = next.userinfo = NULL;

  req_refresh_header_flags(req);
  zlib_reset(req);
  free(req->location);
  req->location = NULL;
  req->pstate = HP_HEADERS;
  req->bmode = BODY_NONE;
  req->got_headers = false;
  req->status = 0;
  req->status_text[0] = '\0';
  req->hdr_len = 0;
  req->line_len = 0;
  req->body_remaining = 0;
  req->chunk_remaining = 0;
  req->tls_ready = false;
  req->tls_socket_eof = false;
  req->tls_sent = 0;

  if (!build_request(req)) {
    req_fail(req, "TypeError", "out of memory");
    return;
  }

  req_start_dns(req);
}

static void req_start_dns(tp_http_request *req)
{
  char port[16];
  snprintf(port, sizeof(port), "%d", req->port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  req->resolver.data = req;
  req->resolver_pending = true;
  int rc = uv_getaddrinfo(g_loop, &req->resolver, on_resolve, req->host, port,
                          &hints);
  if (rc < 0) {
    req->resolver_pending = false;
    req_fail(req, "TypeError", "DNS lookup failed: %s", uv_strerror(rc));
  }
}

static void req_start(tp_http_request *req)
{
  req->start_ms = tp_now_ms();
  req->pstate = HP_HEADERS;
  req->bmode = BODY_NONE;
  if (!build_request(req)) {
    req_fail(req, "TypeError", "out of memory");
    return;
  }

  req->next = g_requests;
  g_requests = req;
  g_active++;

  if (req->timeout_ms > 0) {
    if (uv_timer_init(g_loop, &req->timer) < 0) {
      req_fail(req, "TypeError", "cannot create request timer");
      return;
    }
    req->timer_initialized = true;
    req->timer.data = req;
    uv_timer_start(&req->timer, on_req_timeout, (uint64_t)req->timeout_ms, 0);
  }

  req_start_dns(req);
}

/* ------------------------------------------------------------------------- */
/* libuv callbacks                                                           */
/* ------------------------------------------------------------------------- */

static void req_try_connect(tp_http_request *req)
{
  while (req->addr_next) {
    struct addrinfo *ai = req->addr_next;
    req->addr_next = ai->ai_next;
    if (!ai->ai_addr || ai->ai_addrlen > sizeof(req->peer))
      continue;
    if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
      continue;

    memcpy(&req->peer, ai->ai_addr, ai->ai_addrlen);
    req->peer_len = (socklen_t)ai->ai_addrlen;

    if (!req->tcp_initialized) {
      if (uv_tcp_init(g_loop, &req->tcp) < 0) {
        req_fail(req, "TypeError", "cannot create TCP handle");
        return;
      }
      req->tcp_initialized = true;
      req->tcp.data = req;
      uv_tcp_nodelay(&req->tcp, 1);
    }

    req->connect_pending = true;
    req->connect_req.data = req;
    int rc = uv_tcp_connect(&req->connect_req, &req->tcp,
                            (const struct sockaddr *)&req->peer, on_connect);
    if (rc < 0) {
      /* The handle cannot be reused after a synchronous connect error; close
       * it and continue with the next address from on_tcp_close(). */
      req->connect_pending = false;
      req->last_connect_error = rc;
      if (!req->tcp_closing) {
        req->connect_retry = true;
        req->tcp_closing = true;
        uv_close((uv_handle_t *)&req->tcp, on_tcp_close);
      }
      return;
    }
    return;
  }

  int err = req->last_connect_error ? req->last_connect_error : UV_ECONNREFUSED;
  req_fail(req, "TypeError", "connect failed: %s", uv_strerror(err));
}

static void on_resolve(uv_getaddrinfo_t *resolver, int status,
                       struct addrinfo *res)
{
  tp_http_request *req = (tp_http_request *)resolver->data;
  req->resolver_pending = false;
  if (req->closing) {
    if (res)
      uv_freeaddrinfo(res);
    req_maybe_free(req);
    return;
  }
  if (status < 0) {
    if (res)
      uv_freeaddrinfo(res);
    req_fail(req, "TypeError", "DNS lookup failed: %s", uv_strerror(status));
    return;
  }
  if (!res) {
    req_fail(req, "TypeError", "DNS lookup returned no address");
    return;
  }
  req_free_addrs(req);
  req->addr_list = res;
  req->addr_next = res;
  req->last_connect_error = 0;
  req_try_connect(req);
}

static void on_connect(uv_connect_t *connect, int status)
{
  tp_http_request *req = (tp_http_request *)connect->data;
  req->connect_pending = false;
  if (req->closing) {
    req_maybe_free(req);
    return;
  }
  if (status < 0) {
    req->last_connect_error = status;
    if (req->addr_next && req->tcp_initialized && !req->tcp_closing) {
      req->connect_retry = true;
      req->tcp_closing = true;
      uv_close((uv_handle_t *)&req->tcp, on_tcp_close);
      return;
    }
    req_fail(req, "TypeError", "connect failed: %s", uv_strerror(status));
    return;
  }
  req_free_addrs(req);
  if (req->https) {
    SSL_CTX *ctx = ssl_ctx_get();
    if (!ctx) {
      req_fail(req, "TypeError", "cannot initialize TLS context");
      return;
    }
    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
      req_fail(req, "TypeError", "cannot create TLS session");
      return;
    }
    BIO *bio1 = NULL;
    BIO *bio2 = NULL;
    if (BIO_new_bio_pair(&bio1, 0, &bio2, 0) != 1) {
      SSL_free(ssl);
      req_fail(req, "TypeError", "cannot create TLS buffers");
      return;
    }
    SSL_set_bio(ssl, bio1, bio1);
    SSL_set_connect_state(ssl);
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE |
                      SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    if (!is_ip_address(req->host))
      SSL_set_tlsext_host_name(ssl, req->host);
    if (!req->reject_unauthorized) {
      SSL_set_verify(ssl, SSL_VERIFY_NONE, NULL);
    } else if (is_ip_address(req->host)) {
      X509_VERIFY_PARAM *param = SSL_get0_param(ssl);
      X509_VERIFY_PARAM_set1_ip_asc(param, req->host);
    } else {
      if (SSL_set1_host(ssl, req->host) != 1) {
        SSL_free(ssl);
        BIO_free(bio2);
        req_fail(req, "TypeError", "cannot set TLS hostname");
        return;
      }
    }
    req->ssl = ssl;
    req->ssl_bio = bio1;
    req->net_bio = bio2;
    req->tls_ready = false;
    req->tls_socket_eof = false;
    req->tls_sent = 0;
    tls_pump(req);
    return;
  }

  req->write_owned = false;
  req->write_req.data = req;
  uv_buf_t b = uv_buf_init(req->request_data,
                           (unsigned int)req->request_len);
  int rc = uv_write(&req->write_req, (uv_stream_t *)&req->tcp, &b, 1,
                    on_write);
  if (rc < 0) {
    req_fail(req, "TypeError", "write failed: %s", uv_strerror(rc));
    return;
  }
  req->write_pending = true;
}

static void on_write(uv_write_t *write, int status)
{
  tp_http_request *req = (tp_http_request *)write->data;
  if (req->write_owned) {
    free(req->write_data);
    req->write_data = NULL;
    req->write_owned = false;
  }
  req->write_pending = false;
  if (req->closing) {
    req_maybe_free(req);
    return;
  }
  if (status < 0) {
    req_fail(req, "TypeError", "write failed: %s", uv_strerror(status));
    return;
  }
  if (req->restart_pending) {
    req_try_restart(req);
    return;
  }
  if (req->https)
    tls_pump(req);
  else
    req_start_read(req);
}

static void on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
  tp_http_request *req = (tp_http_request *)handle->data;
  (void)suggested_size;
  buf->base = req->read_buf;
  buf->len = sizeof(req->read_buf);
}

static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
  tp_http_request *req = (tp_http_request *)stream->data;
  if (nread < 0) {
    if (nread == UV_EOF) {
      req->tcp_read_started = false;
      if (req->https) {
        req->tls_socket_eof = true;
        tls_pump(req);
        if (!req->finished && !req->closing)
          req_eof(req);
      } else {
        req_eof(req);
      }
    } else if (nread != UV_ECANCELED && !req->closing) {
      req_fail(req, "TypeError", "read failed: %s", uv_strerror((int)nread));
    }
    return;
  }
  if (nread == 0 || req->closing || req->finished)
    return;

  if (req->https) {
    size_t off = 0;
    while (off < (size_t)nread) {
      int n = BIO_write(req->net_bio, buf->base + off, (int)(nread - off));
      if (n <= 0) {
        req_fail(req, "TypeError", "TLS buffer write failed");
        return;
      }
      off += (size_t)n;
    }
    tls_pump(req);
  } else {
    http_feed_data(req, buf->base, (size_t)nread);
  }
}

static void req_start_read(tp_http_request *req)
{
  if (req->closing || req->finished || req->tcp_read_started)
    return;
  int rc = uv_read_start((uv_stream_t *)&req->tcp, on_alloc, on_read);
  if (rc < 0) {
    req_fail(req, "TypeError", "cannot start read: %s", uv_strerror(rc));
    return;
  }
  req->tcp_read_started = true;
}

/* ------------------------------------------------------------------------- */
/* Loop integration                                                          */
/* ------------------------------------------------------------------------- */

static int loop_init(void)
{
  if (g_loop_ready)
    return 0;
  if (uv_loop_init(&g_loop_storage) < 0)
    return -1;
  g_loop = &g_loop_storage;
  if (uv_timer_init(g_loop, &g_wait_timer) < 0) {
    uv_loop_close(g_loop);
    g_loop = NULL;
    return -1;
  }
  g_wait_timer_ready = true;
  g_loop_ready = true;
  return 0;
}

static void on_poll_event(uv_poll_t *handle, int status, int events)
{
  tp_wait_watch *w = (tp_wait_watch *)handle->data;
  if (!w)
    return;
  if (status < 0 || (events & (UV_READABLE | UV_DISCONNECT)))
    g_stdin_ready = true;
}

static void on_signal_event(uv_poll_t *handle, int status, int events)
{
  tp_wait_watch *w = (tp_wait_watch *)handle->data;
  (void)events;
  if (!w || status < 0)
    return;
  char buf[64];
  while (read(w->fd, buf, sizeof(buf)) > 0) {
  }
  g_signal_ready = true;
}

static void watch_on_close(uv_handle_t *h)
{
  free(h->data);
}

static void watch_close(tp_wait_watch **pw)
{
  tp_wait_watch *w = *pw;
  if (!w)
    return;
  *pw = NULL;
  if (w->active) {
    uv_poll_stop(&w->handle);
    w->active = false;
  }
  uv_close((uv_handle_t *)&w->handle, watch_on_close);
}

static bool watch_start(tp_wait_watch **pw, int fd, bool is_stdin)
{
  if (fd < 0) {
    watch_close(pw);
    return false;
  }
  tp_wait_watch *w = *pw;
  if (w && w->fd != fd) {
    watch_close(pw);
    w = NULL;
  }
  if (!w) {
    w = calloc(1, sizeof(*w));
    if (!w)
      return false;
    if (uv_poll_init(g_loop, &w->handle, fd) < 0) {
      free(w);
      return false;
    }
    w->fd = fd;
    w->is_stdin = is_stdin;
    w->handle.data = w;
    *pw = w;
  }
  if (!w->active) {
    uv_poll_cb cb = is_stdin ? on_poll_event : on_signal_event;
    if (uv_poll_start(&w->handle, UV_READABLE, cb) < 0) {
      watch_close(pw);
      return false;
    }
    w->active = true;
  }
  return true;
}

static void on_wait_timeout(uv_timer_t *timer)
{
  (void)timer;
}

int tp_http_wait(int stdin_fd, int signal_fd, int timeout_ms)
{
  if (!g_loop_ready && loop_init() < 0)
    return 0;
  if (g_running)
    return 0;
  g_stdin_ready = false;
  g_signal_ready = false;
  watch_start(&g_stdin_watch, stdin_fd, true);
  watch_start(&g_signal_watch, signal_fd, false);

  /* With no fd and no request there is nothing that could ever wake an
   * infinite wait. */
  if (timeout_ms < 0 && !g_stdin_watch && !g_signal_watch && g_active == 0)
    return 0;

  g_running = true;
  if (timeout_ms == 0) {
    uv_run(g_loop, UV_RUN_NOWAIT);
  } else {
    if (timeout_ms > 0 && g_wait_timer_ready)
      uv_timer_start(&g_wait_timer, on_wait_timeout, (uint64_t)timeout_ms, 0);
    uv_run(g_loop, UV_RUN_ONCE);
    if (g_wait_timer_ready)
      uv_timer_stop(&g_wait_timer);
  }
  g_running = false;

  int flags = 0;
  if (g_stdin_ready)
    flags |= 1;
  if (g_signal_ready)
    flags |= 2;
  return flags;
}

int tp_http_run(int timeout_ms)
{
  if (!g_loop_ready)
    return g_active == 0 ? 1 : 0;
  if (g_running)
    return -1;

  g_running = true;
  if (g_wait_timer_ready)
    uv_timer_stop(&g_wait_timer);

  if (g_active > 0) {
    if (timeout_ms == 0) {
      uv_run(g_loop, UV_RUN_NOWAIT);
    } else if (timeout_ms < 0) {
      /* The loop returns once every request handle has been closed and the
       * registry references have been released. */
      uv_run(g_loop, UV_RUN_DEFAULT);
    } else {
      double deadline = tp_now_ms() + (double)timeout_ms;
      while (g_active > 0) {
        double remain = deadline - tp_now_ms();
        if (remain <= 0)
          break;
        if (g_wait_timer_ready)
          uv_timer_start(&g_wait_timer, on_wait_timeout,
                         (uint64_t)(remain < 1 ? 1 : remain), 0);
        uv_run(g_loop, UV_RUN_ONCE);
        if (g_wait_timer_ready)
          uv_timer_stop(&g_wait_timer);
      }
    }
  }
  g_running = false;
  return g_active == 0 ? 1 : 0;
}

void tp_http_shutdown(lua_State *L)
{
  while (g_requests) {
    tp_http_request *req = g_requests;
    if (L)
      req->L = L;
    req->silent = true;
    req->finished = true;
    req_close(req);
    if (g_requests == req) {
      if (g_running)
        break;
      if (req->resolver_pending || req->connect_pending ||
          req->write_pending || req->tcp_initialized ||
          req->timer_initialized)
        break;
      req_unlink(req);
      req_free_now(req);
    }
  }
  if (!g_loop_ready || g_running)
    return;

  /* DNS cancellation and handle close callbacks need real loop iterations. */
  double deadline = tp_now_ms() + 2000.0;
  while (g_requests && tp_now_ms() < deadline) {
    if (g_wait_timer_ready)
      uv_timer_start(&g_wait_timer, on_wait_timeout, 10, 0);
    uv_run(g_loop, UV_RUN_ONCE);
    if (g_wait_timer_ready)
      uv_timer_stop(&g_wait_timer);
  }
}

void tp_http_free(void)
{
  if (g_loop_ready) {
    /* The Lua state is gone by now.  Never call back into it while draining
     * any request that survived shutdown. */
    for (tp_http_request *req = g_requests; req; req = req->next) {
      req->L = NULL;
      req->silent = true;
      req->finished = true;
    }
    while (g_requests) {
      tp_http_request *req = g_requests;
      req_close(req);
      if (g_requests == req) {
        if (req->resolver_pending || req->connect_pending ||
            req->write_pending || req->tcp_initialized ||
            req->timer_initialized)
          break;
        req_unlink(req);
        req_free_now(req);
      }
    }

    watch_close(&g_stdin_watch);
    watch_close(&g_signal_watch);
    if (g_wait_timer_ready) {
      uv_timer_stop(&g_wait_timer);
      uv_close((uv_handle_t *)&g_wait_timer, NULL);
      g_wait_timer_ready = false;
    }
    uv_run(g_loop, UV_RUN_DEFAULT);
    if (uv_loop_close(g_loop) != 0) {
      uv_run(g_loop, UV_RUN_NOWAIT);
      uv_loop_close(g_loop);
    }
    g_loop = NULL;
    g_loop_ready = false;
  }
  if (g_ssl_ctx) {
    SSL_CTX_free(g_ssl_ctx);
    g_ssl_ctx = NULL;
  }
}

bool tp_http_active(void)
{
  return g_active > 0;
}

/* ------------------------------------------------------------------------- */
/* Lua binding                                                               */
/* ------------------------------------------------------------------------- */

static tp_http_request *check_request(lua_State *L, int idx)
{
  tp_http_request **ud = luaL_checkudata(L, idx, HTTP_UD_META);
  return *ud;
}

static const char *opt_string(lua_State *L, int idx, const char *name,
                              const char *def)
{
  lua_getfield(L, idx, name);
  const char *s = def;
  if (!lua_isnil(L, -1))
    s = lua_tostring(L, -1);
  lua_pop(L, 1);
  return s;
}

static bool opt_bool(lua_State *L, int idx, const char *name, bool def)
{
  lua_getfield(L, idx, name);
  bool v = def;
  if (!lua_isnil(L, -1))
    v = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return v;
}

static long opt_int(lua_State *L, int idx, const char *name, long def)
{
  lua_getfield(L, idx, name);
  long v = def;
  if (!lua_isnil(L, -1))
    v = (long)lua_tointeger(L, -1);
  lua_pop(L, 1);
  return v;
}

static int parse_lua_headers(lua_State *L, tp_http_request *req, int idx)
{
  lua_getfield(L, idx, "headers");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return -1;
  }
  int n = (int)lua_objlen(L, -1);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, -1, i);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 2);
      return -1;
    }
    lua_rawgeti(L, -1, 1);
    lua_rawgeti(L, -2, 2);
    const char *name = lua_tostring(L, -2);
    const char *value = lua_tostring(L, -1);
    if (!name || !value || !header_name_valid(name) ||
        !header_value_valid(value)) {
      lua_pop(L, 4);
      return -1;
    }
    char *ncopy = xstrdup(name);
    char *vcopy = xstrdup(value);
    bool forbidden = ncopy && strcasecmp(ncopy, "Transfer-Encoding") == 0;
    lua_pop(L, 3); /* value, name, pair -- keep the headers table */
    if (!ncopy || !vcopy) {
      free(ncopy);
      free(vcopy);
      lua_pop(L, 1);
      return -1;
    }
    if (forbidden) {
      free(ncopy);
      free(vcopy);
      lua_pop(L, 1);
      return -2;
    }
    if (!headers_push(req, ncopy, vcopy)) {
      free(ncopy);
      free(vcopy);
      lua_pop(L, 1);
      return -1;
    }
  }
  lua_pop(L, 1);
  return 0;
}

static int l_request(lua_State *L)
{
  const char *url = luaL_checkstring(L, 1);
  if (!lua_isnoneornil(L, 2) && !lua_istable(L, 2))
    return luaL_error(L, "fetch options must be a table");
  if (!lua_istable(L, 3))
    return luaL_error(L, "fetch callbacks must be a table");

  if (loop_init() < 0)
    return luaL_error(L, "cannot initialize network loop");

  tp_http_request **ud = lua_newuserdata(L, sizeof(*ud));
  *ud = NULL;
  luaL_getmetatable(L, HTTP_UD_META);
  lua_setmetatable(L, -2);
  int ud_index = lua_gettop(L);

  tp_http_request *req = calloc(1, sizeof(*req));
  if (!req)
    return luaL_error(L, "out of memory");
  req->L = L;
  req->ud = ud;
  req->cb_ref = LUA_NOREF;
  req->ud_ref = LUA_NOREF;
  req->redirect_mode = REDIRECT_FOLLOW;
  req->max_redirects = HTTP_MAX_REDIRECTS;
  req->reject_unauthorized = true;
  req->decompress = true;
  req->timeout_ms = HTTP_DEFAULT_TIMEOUT_MS;
  *ud = req;

  lua_pushvalue(L, ud_index);
  req->ud_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_pushvalue(L, 3);
  req->cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

  int opts = lua_isnoneornil(L, 2) ? 0 : 2;
  if (opts) {
    const char *method = opt_string(L, opts, "method", "GET");
    if (!method || !*method)
      method = "GET";
    req->method = xstrdup(method);
    if (!req->method)
      goto oom;
    for (char *p = req->method; *p; p++)
      *p = (char)toupper((unsigned char)*p);

    lua_getfield(L, opts, "body");
    if (!lua_isnil(L, -1)) {
      if (!lua_isstring(L, -1)) {
        lua_pop(L, 1);
        req_free_now(req);
        return luaL_error(L, "fetch body must be a string");
      }
      size_t blen = 0;
      const char *body = lua_tolstring(L, -1, &blen);
      req->body = malloc(blen + 1);
      if (!req->body) {
        lua_pop(L, 1);
        goto oom;
      }
      memcpy(req->body, body, blen);
      req->body[blen] = '\0';
      req->body_len = blen;
      req->body_present = true;
    }
    lua_pop(L, 1);

    const char *redirect = opt_string(L, opts, "redirect", "follow");
    if (redirect && strcasecmp(redirect, "manual") == 0)
      req->redirect_mode = REDIRECT_MANUAL;
    else if (redirect && strcasecmp(redirect, "error") == 0)
      req->redirect_mode = REDIRECT_ERROR;
    else if (!redirect || strcasecmp(redirect, "follow") != 0) {
      req_free_now(req);
      return luaL_error(L, "unsupported redirect mode");
    }
    req->max_redirects = (int)opt_int(L, opts, "max_redirects",
                                      HTTP_MAX_REDIRECTS);
    if (req->max_redirects < 0)
      req->max_redirects = 0;
    if (req->max_redirects > HTTP_MAX_REDIRECTS)
      req->max_redirects = HTTP_MAX_REDIRECTS;
    req->reject_unauthorized =
        opt_bool(L, opts, "reject_unauthorized", true);
    req->decompress = opt_bool(L, opts, "decompress", true);
    req->timeout_ms = opt_int(L, opts, "timeout_ms",
                              HTTP_DEFAULT_TIMEOUT_MS);

    int hr = parse_lua_headers(L, req, opts);
    if (hr == -2) {
      req_free_now(req);
      return luaL_error(L, "request Transfer-Encoding is not supported");
    }
    if (hr < 0) {
      req_free_now(req);
      return luaL_error(L, "invalid fetch headers");
    }
  } else {
    req->method = xstrdup("GET");
    if (!req->method)
      goto oom;
  }

  if (!header_name_valid(req->method)) {
    req_free_now(req);
    return luaL_error(L, "invalid HTTP method");
  }

  if ((strcasecmp(req->method, "GET") == 0 ||
       strcasecmp(req->method, "HEAD") == 0) &&
      req->body_present) {
    req_free_now(req);
    return luaL_error(L, "fetch with GET/HEAD cannot have a body");
  }

  tp_url parsed;
  char err[256];
  if (parse_url(url, &parsed, err, sizeof(err)) < 0) {
    req_free_now(req);
    return luaL_error(L, "%s", err);
  }
  req->https = parsed.https;
  req->host = parsed.host;
  req->host_header = parsed.host_header;
  req->host_is_ipv6 = parsed.host_is_ipv6;
  req->port = parsed.port;
  req->path = parsed.path;
  req->current_url = parsed.url;
  req->userinfo = parsed.userinfo;
  req_refresh_header_flags(req);

  req_start(req);
  lua_settop(L, ud_index);
  return 1;

oom:
  req_free_now(req);
  return luaL_error(L, "out of memory");
}

static int l_abort(lua_State *L)
{
  tp_http_request *req = check_request(L, 1);
  if (!req || req->finished) {
    lua_pushboolean(L, 0);
    return 1;
  }
  req_fail(req, "AbortError", "The operation was aborted");
  lua_pushboolean(L, 1);
  return 1;
}

static int l_run(lua_State *L)
{
  int timeout = (int)luaL_optinteger(L, 1, 0);
  lua_pushinteger(L, tp_http_run(timeout));
  return 1;
}

static int l_pending(lua_State *L)
{
  lua_pushinteger(L, g_active);
  return 1;
}

static int l_ticks(lua_State *L)
{
  lua_pushnumber(L, tp_now_ms());
  return 1;
}

static int l_version(lua_State *L)
{
  lua_createtable(L, 0, 4);
  lua_pushstring(L, uv_version_string());
  lua_setfield(L, -2, "libuv");
  lua_pushstring(L, OpenSSL_version(OPENSSL_VERSION));
  lua_setfield(L, -2, "openssl");
  lua_pushboolean(L, g_loop_ready ? 1 : 0);
  lua_setfield(L, -2, "initialized");
  return 1;
}

static int l_req_gc(lua_State *L)
{
  tp_http_request **ud = luaL_checkudata(L, 1, HTTP_UD_META);
  tp_http_request *req = *ud;
  if (!req)
    return 0;
  *ud = NULL;
  req->ud = NULL;
  req->L = L;
  req->silent = true;
  req->finished = true;
  req_close(req);
  return 0;
}

static int l_req_tostring(lua_State *L)
{
  tp_http_request **ud = luaL_checkudata(L, 1, HTTP_UD_META);
  if (!*ud)
    lua_pushliteral(L, "tuipro.http.request (closed)");
  else
    lua_pushfstring(L, "tuipro.http.request (%s %s)", (*ud)->method,
                    (*ud)->current_url ? (*ud)->current_url : "");
  return 1;
}

static const luaL_Reg http_funcs[] = {
  { "request", l_request },
  { "abort", l_abort },
  { "run", l_run },
  { "pending", l_pending },
  { "ticks", l_ticks },
  { "version", l_version },
  { NULL, NULL },
};

int luaopen_tuipro_http(lua_State *L)
{
  luaL_newmetatable(L, HTTP_UD_META);
  lua_pushcfunction(L, l_req_gc);
  lua_setfield(L, -2, "__gc");
  lua_pushcfunction(L, l_req_tostring);
  lua_setfield(L, -2, "__tostring");
  lua_pop(L, 1);

  luaL_newlib(L, http_funcs);
  return 1;
}

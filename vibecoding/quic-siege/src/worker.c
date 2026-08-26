/* worker.c -- one lsquic client engine + epoll reactor per worker thread.
 *
 * Design: each QUIC connection gets its own connect()ed UDP socket. This
 * avoids IP_PKTINFO/ECN cmsg bookkeeping that a shared socket would need
 * (the kernel handles source-address selection and demuxes replies for
 * us), at the cost of one fd per in-flight connection -- fine up to the
 * thousands-of-fds range typical of a siege run; raise the process'
 * RLIMIT_NOFILE if you need more concurrency than that.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <openssl/ssl.h>

#include "lsquic.h"
#include "siege.h"

#define QS_MAX_EVENTS   256
#define QS_RECV_BUF_SZ  65536
#define QS_READ_BUF_SZ  4096

/* ---- globals shared read-only by all worker threads after init ---- */
static unsigned char *g_payload_buf;
static size_t         g_payload_len;
static SSL_CTX        *g_verify_ssl_ctx; /* only set when --verify-cert */

/* ---- per-connection socket wrapper (also the lsquic peer_ctx) ---- */
struct qs_conn_sock {
    int                     fd;
    struct sockaddr_storage local_sa;
    socklen_t               local_sa_len;
    struct sockaddr_storage peer_sa;
    socklen_t               peer_sa_len;
    struct qs_worker        *worker;
    int                     epollout_on;
    double                  connect_call_time_us;
    lsquic_conn_t           *conn;   /* set once on_new_conn fires; NULL until then */
    struct qs_conn_sock     *prev, *next; /* intrusive list: worker->active_head */
};

/* ---- per-connection application context (lsquic_conn_ctx_t) ---- */
struct lsquic_conn_ctx {
    lsquic_conn_t        *conn;
    struct qs_conn_sock  *cs;
    struct qs_worker     *worker;
    double                connect_time_us;
    unsigned              streams_open;
};

/* ---- per-stream application context (lsquic_stream_ctx_t) ---- */
struct lsquic_stream_ctx {
    lsquic_stream_t         *stream;
    struct lsquic_conn_ctx  *conn_ctx;
    double                   open_time_us;
    size_t                   bytes_written;
};

/* ---- worker state (not shared across threads) ---- */
struct qs_worker {
    const struct qs_config *cfg;
    unsigned                 id;
    lsquic_engine_t          *engine;
    int                       epfd;
    struct qs_stats           stats;

    unsigned  concurrency_share;   /* this worker's slice of --concurrency */
    uint64_t  target_count;        /* this worker's slice of --connections, or UINT64_MAX */
    unsigned  open_count;          /* connections currently live */
    uint64_t  opened_total;        /* connect() attempts issued so far */

    double    start_time_us;
    double    spawn_interval_us;   /* 0 = unlimited */
    double    next_spawn_time_us;

    volatile sig_atomic_t *stop_flag;
    int       draining;
    double    drain_deadline_us;
    int       force_closed;

    struct qs_conn_sock *active_head;
};

static double
now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec * 1e6 + (double) ts.tv_nsec / 1e3;
}

/* ------------------------------------------------------------------ */
/* connection socket lifecycle                                        */
/* ------------------------------------------------------------------ */

static void
list_add(struct qs_worker *w, struct qs_conn_sock *cs)
{
    cs->prev = NULL;
    cs->next = w->active_head;
    if (w->active_head)
        w->active_head->prev = cs;
    w->active_head = cs;
}

static void
list_remove(struct qs_worker *w, struct qs_conn_sock *cs)
{
    if (cs->prev) cs->prev->next = cs->next;
    else w->active_head = cs->next;
    if (cs->next) cs->next->prev = cs->prev;
}

/* Returns NULL and bumps stats.conns_failed on any setup failure. */
static struct qs_conn_sock *
conn_sock_new(struct qs_worker *w)
{
    const struct addrinfo *ai = w->cfg->resolved;
    struct qs_conn_sock *cs = calloc(1, sizeof(*cs));
    if (!cs)
        return NULL;

    cs->fd = socket(ai->ai_family, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (cs->fd < 0)
    {
        free(cs);
        return NULL;
    }
    if (0 != connect(cs->fd, ai->ai_addr, ai->ai_addrlen))
    {
        close(cs->fd);
        free(cs);
        return NULL;
    }

    cs->local_sa_len = sizeof(cs->local_sa);
    if (0 != getsockname(cs->fd, (struct sockaddr *) &cs->local_sa, &cs->local_sa_len))
    {
        close(cs->fd);
        free(cs);
        return NULL;
    }
    memcpy(&cs->peer_sa, ai->ai_addr, ai->ai_addrlen);
    cs->peer_sa_len = ai->ai_addrlen;
    cs->worker = w;

    struct epoll_event ev = { .events = EPOLLIN, .data.ptr = cs };
    if (0 != epoll_ctl(w->epfd, EPOLL_CTL_ADD, cs->fd, &ev))
    {
        close(cs->fd);
        free(cs);
        return NULL;
    }

    list_add(w, cs);
    return cs;
}

static void
conn_sock_free(struct qs_worker *w, struct qs_conn_sock *cs)
{
    epoll_ctl(w->epfd, EPOLL_CTL_DEL, cs->fd, NULL);
    list_remove(w, cs);
    close(cs->fd);
    free(cs);
}

static void
conn_sock_want_write(struct qs_worker *w, struct qs_conn_sock *cs, int want)
{
    if (want == cs->epollout_on)
        return;
    struct epoll_event ev = { .events = (uint32_t)(EPOLLIN | (want ? EPOLLOUT : 0)), .data.ptr = cs };
    epoll_ctl(w->epfd, EPOLL_CTL_MOD, cs->fd, &ev);
    cs->epollout_on = want;
}

/* ------------------------------------------------------------------ */
/* lsquic engine callbacks                                            */
/* ------------------------------------------------------------------ */

static SSL_CTX *
qs_get_ssl_ctx(void *peer_ctx, const struct sockaddr *local_sa)
{
    (void) peer_ctx;
    (void) local_sa;
    return g_verify_ssl_ctx; /* only installed as ea_get_ssl_ctx when --verify-cert is set */
}

static lsquic_conn_ctx_t *
qs_on_new_conn(void *stream_if_ctx, lsquic_conn_t *c)
{
    struct qs_worker *w = stream_if_ctx;
    struct qs_conn_sock *cs = lsquic_conn_get_peer_ctx(c, NULL);
    cs->conn = c;

    struct lsquic_conn_ctx *cctx = calloc(1, sizeof(*cctx));
    cctx->conn = c;
    cctx->cs = cs;
    cctx->worker = w;
    cctx->connect_time_us = cs->connect_call_time_us;
    cctx->streams_open = 0;

    for (unsigned i = 0; i < w->cfg->streams_per_conn; ++i)
        lsquic_conn_make_stream(c);

    return cctx;
}

static void
qs_on_hsk_done(lsquic_conn_t *c, enum lsquic_hsk_status s)
{
    struct lsquic_conn_ctx *cctx = lsquic_conn_get_ctx(c);
    if (!cctx)
        return;
    struct qs_worker *w = cctx->worker;
    qs_sample_add(&w->stats.handshake_us, now_us() - cctx->connect_time_us);

    if (s == LSQ_HSK_OK || s == LSQ_HSK_RESUMED_OK)
        ++w->stats.conns_established;
    else
    {
        ++w->stats.conns_failed;
        lsquic_conn_close(c);
    }
}

static void
qs_on_conn_closed(lsquic_conn_t *c)
{
    struct lsquic_conn_ctx *cctx = lsquic_conn_get_ctx(c);
    if (!cctx)
        return;
    struct qs_worker *w = cctx->worker;

    if (cctx->streams_open == 0)
        /* handshake never completed and no streams were ever opened */;
    else
        ++w->stats.conns_reset; /* closed with streams still outstanding */

    conn_sock_free(w, cctx->cs);
    lsquic_conn_set_ctx(c, NULL);
    free(cctx);

    if (w->open_count > 0)
        --w->open_count;
}

static lsquic_stream_ctx_t *
qs_on_new_stream(void *stream_if_ctx, lsquic_stream_t *s)
{
    (void) stream_if_ctx;
    struct lsquic_conn_ctx *cctx = lsquic_conn_get_ctx(lsquic_stream_conn(s));
    struct lsquic_stream_ctx *sctx = calloc(1, sizeof(*sctx));
    sctx->stream = s;
    sctx->conn_ctx = cctx;
    sctx->open_time_us = now_us();
    sctx->bytes_written = 0;

    if (cctx)
    {
        ++cctx->streams_open;
        ++cctx->worker->stats.streams_opened;
    }
    lsquic_stream_wantwrite(s, 1);
    return sctx;
}

static void
qs_on_write(lsquic_stream_t *s, lsquic_stream_ctx_t *h)
{
    const struct qs_config *cfg = h->conn_ctx->worker->cfg;
    struct qs_stats *stats = &h->conn_ctx->worker->stats;

    if (h->bytes_written < cfg->payload_size)
    {
        size_t remaining = cfg->payload_size - h->bytes_written;
        if (remaining > g_payload_len)
            remaining = g_payload_len;
        ssize_t n = lsquic_stream_write(s, g_payload_buf, remaining);
        if (n > 0)
        {
            h->bytes_written += (size_t) n;
            stats->bytes_sent += (uint64_t) n;
        }
        else if (n < 0)
        {
            ++stats->streams_failed;
            lsquic_stream_wantwrite(s, 0);
            lsquic_stream_close(s);
            return;
        }
    }

    if (h->bytes_written >= cfg->payload_size)
    {
        lsquic_stream_flush(s);
        lsquic_stream_wantwrite(s, 0);
        if (cfg->wait_response)
        {
            lsquic_stream_shutdown(s, 1); /* done sending; half-close write side */
            lsquic_stream_wantread(s, 1);
        }
        else
        {
            ++stats->streams_completed;
            lsquic_stream_close(s);
        }
    }
}

static void
qs_on_read(lsquic_stream_t *s, lsquic_stream_ctx_t *h)
{
    struct qs_stats *stats = &h->conn_ctx->worker->stats;
    unsigned char buf[QS_READ_BUF_SZ];

    for (;;)
    {
        ssize_t n = lsquic_stream_read(s, buf, sizeof(buf));
        if (n > 0)
        {
            stats->bytes_recv += (uint64_t) n;
            continue;
        }
        else if (n == 0)
        {
            qs_sample_add(&stats->stream_us, now_us() - h->open_time_us);
            ++stats->streams_completed;
            lsquic_stream_wantread(s, 0);
            lsquic_stream_close(s);
            return;
        }
        else
        {
            if (errno == EWOULDBLOCK)
                return;
            ++stats->streams_failed;
            lsquic_stream_wantread(s, 0);
            lsquic_stream_close(s);
            return;
        }
    }
}

static void
qs_on_close(lsquic_stream_t *s, lsquic_stream_ctx_t *h)
{
    (void) s;
    struct lsquic_conn_ctx *cctx = h->conn_ctx;
    if (cctx)
    {
        if (cctx->streams_open > 0)
            --cctx->streams_open;
        if (cctx->streams_open == 0 && 0 == lsquic_conn_n_pending_streams(cctx->conn))
            lsquic_conn_close(cctx->conn);
    }
    free(h);
}

static const struct lsquic_stream_if qs_stream_if = {
    .on_new_conn    = qs_on_new_conn,
    .on_conn_closed = qs_on_conn_closed,
    .on_new_stream  = qs_on_new_stream,
    .on_read        = qs_on_read,
    .on_write       = qs_on_write,
    .on_close       = qs_on_close,
    .on_hsk_done    = qs_on_hsk_done,
};

/* ------------------------------------------------------------------ */
/* packets_out                                                        */
/* ------------------------------------------------------------------ */

static int
qs_packets_out(void *ctx, const struct lsquic_out_spec *specs, unsigned n)
{
    struct qs_worker *w = ctx;
    unsigned n_sent = 0;

    for (; n_sent < n; ++n_sent)
    {
        struct qs_conn_sock *cs = specs[n_sent].peer_ctx;
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = specs[n_sent].iov;
        msg.msg_iovlen = specs[n_sent].iovlen;

        ssize_t n_w = sendmsg(cs->fd, &msg, 0);
        if (n_w < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                conn_sock_want_write(w, cs, 1);
            break;
        }
        w->stats.bytes_sent += (uint64_t) n_w;
    }

    if (n_sent == 0 && n > 0)
        return -1;
    return (int) n_sent;
}

/* ------------------------------------------------------------------ */
/* connection spawning / rate limiting                                */
/* ------------------------------------------------------------------ */

static void
spawn_one_connection(struct qs_worker *w)
{
    struct qs_conn_sock *cs = conn_sock_new(w);
    ++w->opened_total;
    ++w->stats.conns_attempted;
    if (!cs)
    {
        ++w->stats.conns_failed;
        return;
    }
    cs->connect_call_time_us = now_us();

    lsquic_conn_t *conn = lsquic_engine_connect(
        w->engine, N_LSQVER,
        (struct sockaddr *) &cs->local_sa, (struct sockaddr *) &cs->peer_sa,
        cs /* peer_ctx */, NULL /* conn_ctx */,
        w->cfg->sni, 0 /* base_plpmtu: 0 = let lsquic pick */,
        NULL, 0, NULL, 0);

    if (!conn)
    {
        ++w->stats.conns_failed;
        conn_sock_free(w, cs);
        return;
    }
    ++w->open_count;
}

static int
should_keep_spawning(struct qs_worker *w)
{
    if (w->draining || *w->stop_flag)
        return 0;
    if (w->open_count >= w->concurrency_share)
        return 0;
    if (w->cfg->mode == QS_MODE_COUNT && w->opened_total >= w->target_count)
        return 0;
    if (w->cfg->mode == QS_MODE_DURATION
            && now_us() - w->start_time_us >= w->cfg->duration_s * 1e6)
        return 0;
    return 1;
}

static void
maybe_spawn(struct qs_worker *w)
{
    while (should_keep_spawning(w))
    {
        if (w->spawn_interval_us > 0)
        {
            double t = now_us();
            if (t < w->next_spawn_time_us)
                return; /* not yet; loop timeout will wake us */
            w->next_spawn_time_us =
                (w->next_spawn_time_us + w->spawn_interval_us > t)
                    ? w->next_spawn_time_us + w->spawn_interval_us
                    : t + w->spawn_interval_us;
        }
        spawn_one_connection(w);
    }
}

/* ------------------------------------------------------------------ */
/* drain / shutdown                                                    */
/* ------------------------------------------------------------------ */

static void
maybe_enter_draining(struct qs_worker *w)
{
    if (w->draining)
        return;

    int done_spawning =
        *w->stop_flag
        || (w->cfg->mode == QS_MODE_COUNT && w->opened_total >= w->target_count)
        || (w->cfg->mode == QS_MODE_DURATION
                && now_us() - w->start_time_us >= w->cfg->duration_s * 1e6);
    if (!done_spawning)
        return;

    w->draining = 1;
    double grace_us = *w->stop_flag ? 1.0 * 1e6 : (double) w->cfg->drain_timeout_s * 1e6;
    w->drain_deadline_us = now_us() + grace_us;
}

static void
force_close_stragglers(struct qs_worker *w)
{
    if (w->force_closed)
        return;
    w->force_closed = 1;
    /* Snapshot first: lsquic_conn_close() can synchronously trigger
     * on_conn_closed for some connections (already-going-away ones),
     * which mutates this list via list_remove(), so walk a local copy
     * of next-pointers rather than the live list. */
    for (struct qs_conn_sock *cs = w->active_head; cs; )
    {
        struct qs_conn_sock *next = cs->next;
        if (cs->conn)
            lsquic_conn_close(cs->conn);
        cs = next;
    }
}

/* ------------------------------------------------------------------ */
/* main worker loop                                                    */
/* ------------------------------------------------------------------ */

static int
compute_timeout_ms(struct qs_worker *w)
{
    double now = now_us();
    double next = -1;

    int diff_us;
    if (lsquic_engine_earliest_adv_tick(w->engine, &diff_us))
    {
        double t = now + (diff_us > 0 ? diff_us : 0);
        if (next < 0 || t < next) next = t;
    }
    if (should_keep_spawning(w))
    {
        double t = w->spawn_interval_us > 0 ? w->next_spawn_time_us : now;
        if (next < 0 || t < next) next = t;
    }
    if (w->draining)
    {
        double cap = now + 200000.0;
        double t = w->drain_deadline_us < cap ? w->drain_deadline_us : cap;
        if (next < 0 || t < next) next = t;
    }

    if (next < 0)
        return -1;
    double delta_ms = (next - now) / 1000.0;
    if (delta_ms < 0) delta_ms = 0;
    if (delta_ms > 1000) delta_ms = 1000;
    return (int) delta_ms;
}

void *
qs_worker_run(void *arg)
{
    struct qs_worker_args *wa = arg;
    const struct qs_config *cfg = wa->cfg;

    struct qs_worker w;
    memset(&w, 0, sizeof(w));
    w.cfg = cfg;
    w.id = wa->worker_id;
    w.stop_flag = wa->stop_flag;
    w.target_count = wa->n_connections_share;
    w.start_time_us = now_us();

    unsigned base = cfg->concurrency / cfg->n_threads;
    unsigned rem  = cfg->concurrency % cfg->n_threads;
    w.concurrency_share = base + (w.id < rem ? 1 : 0);
    if (w.concurrency_share == 0)
        w.concurrency_share = 1;

    if (cfg->rate > 0)
        w.spawn_interval_us = 1e6 * cfg->n_threads / cfg->rate;
    w.next_spawn_time_us = w.start_time_us;

    if (0 != qs_stats_init(&w.stats, (unsigned) time(NULL) ^ (w.id * 2654435761u)))
    {
        wa->result.ok = 0;
        return NULL;
    }

    w.epfd = epoll_create1(0);
    if (w.epfd < 0)
    {
        wa->result.ok = 0;
        qs_stats_destroy(&w.stats);
        return NULL;
    }

    struct lsquic_engine_api api;
    memset(&api, 0, sizeof(api));
    struct lsquic_engine_settings settings;
    lsquic_engine_init_settings(&settings, 0 /* client */);
    settings.es_handshake_to = (unsigned) cfg->handshake_timeout_ms * 1000u;
    settings.es_idle_timeout = cfg->idle_timeout_s;

    api.ea_settings = &settings;
    api.ea_stream_if = &qs_stream_if;
    api.ea_stream_if_ctx = &w;
    api.ea_packets_out = qs_packets_out;
    api.ea_packets_out_ctx = &w;
    api.ea_alpn = cfg->alpn;
    if (cfg->verify_cert)
        api.ea_get_ssl_ctx = qs_get_ssl_ctx;

    w.engine = lsquic_engine_new(0 /* client */, &api);
    if (!w.engine)
    {
        wa->result.ok = 0;
        close(w.epfd);
        qs_stats_destroy(&w.stats);
        return NULL;
    }

    struct epoll_event events[QS_MAX_EVENTS];
    while (!(w.draining && w.open_count == 0))
    {
        /* Hard safety valve: never loop more than 5s past the point we
         * force-closed stragglers, even if some connection's teardown
         * callback never fires for an unexpected reason. */
        if (w.force_closed && now_us() >= w.drain_deadline_us + 5.0e6)
            break;

        int timeout_ms = compute_timeout_ms(&w);
        int n = epoll_wait(w.epfd, events, QS_MAX_EVENTS, timeout_ms);

        for (int i = 0; i < n; ++i)
        {
            struct qs_conn_sock *cs = events[i].data.ptr;

            if (events[i].events & EPOLLOUT)
            {
                conn_sock_want_write(&w, cs, 0);
                lsquic_engine_send_unsent_packets(w.engine);
            }
            if (events[i].events & EPOLLIN)
            {
                unsigned char buf[QS_RECV_BUF_SZ];
                for (int iter = 0; iter < 32; ++iter)
                {
                    ssize_t r = recv(cs->fd, buf, sizeof(buf), 0);
                    if (r < 0)
                        break;
                    w.stats.bytes_recv += (uint64_t) r;
                    lsquic_engine_packet_in(w.engine, buf, (size_t) r,
                        (struct sockaddr *) &cs->local_sa,
                        (struct sockaddr *) &cs->peer_sa, cs, 0);
                }
            }
        }

        lsquic_engine_process_conns(w.engine);

        maybe_enter_draining(&w);
        if (w.draining && now_us() >= w.drain_deadline_us)
            force_close_stragglers(&w);
        if (!w.draining)
            maybe_spawn(&w);
    }

    lsquic_engine_destroy(w.engine);
    close(w.epfd);

    wa->result.ok = 1;
    wa->result.stats = w.stats; /* struct copy: buffers are heap-owned, ownership moves to caller */
    return NULL;
}

int
qs_worker_global_init(const struct qs_config *cfg)
{
    if (0 != lsquic_global_init(LSQUIC_GLOBAL_CLIENT))
        return -1;

    g_payload_len = cfg->payload_size > 0 ? cfg->payload_size : 1;
    g_payload_buf = malloc(g_payload_len);
    if (!g_payload_buf)
        return -1;
    static const char pattern[] = "QUICSIEGE-LOAD-PAYLOAD-";
    for (size_t i = 0; i < g_payload_len; ++i)
        g_payload_buf[i] = (unsigned char) pattern[i % (sizeof(pattern) - 1)];

    if (cfg->verify_cert)
    {
        g_verify_ssl_ctx = SSL_CTX_new(TLS_method());
        if (!g_verify_ssl_ctx)
            return -1;
        SSL_CTX_set_default_verify_paths(g_verify_ssl_ctx);
        SSL_CTX_set_verify(g_verify_ssl_ctx, SSL_VERIFY_PEER, NULL);
    }
    return 0;
}

void
qs_worker_global_cleanup(void)
{
    free(g_payload_buf);
    g_payload_buf = NULL;
    if (g_verify_ssl_ctx)
    {
        SSL_CTX_free(g_verify_ssl_ctx);
        g_verify_ssl_ctx = NULL;
    }
    lsquic_global_cleanup();
}

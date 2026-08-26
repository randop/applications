/* siege.h -- shared config and types for quic-siege */
#ifndef QS_SIEGE_H
#define QS_SIEGE_H

#include <stdint.h>
#include <netdb.h>
#include <signal.h>
#include "stats.h"

enum qs_mode {
    QS_MODE_COUNT,     /* stop after --connections total attempts */
    QS_MODE_DURATION,  /* stop after --duration seconds */
};

struct qs_config {
    const char *host;          /* target host, string as given on CLI */
    const char *port;          /* target port, string form for getaddrinfo */
    const char *alpn;          /* ALPN token to negotiate */
    const char *sni;           /* SNI/hostname sent in ClientHello; defaults to host */

    unsigned concurrency;      /* max simultaneous open connections, total */
    unsigned n_threads;

    enum qs_mode mode;
    uint64_t n_connections;    /* target for QS_MODE_COUNT */
    double   duration_s;       /* target for QS_MODE_DURATION */

    unsigned streams_per_conn;
    unsigned payload_size;
    int      wait_response;    /* 1 = read response before closing stream */

    double   rate;             /* global connect attempts/sec, 0 = unlimited */

    unsigned idle_timeout_s;      /* es_idle_timeout is in seconds */
    unsigned handshake_timeout_ms; /* es_handshake_to is in microseconds */
    unsigned drain_timeout_s;     /* grace period at end of run before force-closing stragglers */

    int      quiet;
    int      verify_cert;      /* 1 = verify peer cert against system CAs */

    struct addrinfo *resolved; /* peer address, shared read-only across workers */
};

struct qs_worker_result {
    struct qs_stats stats;
    int              ok;   /* 0 if the worker hit a fatal setup error */
};

/* Entry point run on each worker thread. `arg` is a `struct qs_worker_args *`. */
void *qs_worker_run(void *arg);

/* Must be called once from main(), before any worker threads start, and
 * torn down once after all workers have joined. */
int  qs_worker_global_init(const struct qs_config *cfg);
void qs_worker_global_cleanup(void);

struct qs_worker_args {
    const struct qs_config *cfg;
    unsigned                worker_id;
    uint64_t                n_connections_share; /* this worker's slice in COUNT mode */
    volatile sig_atomic_t  *stop_flag;            /* shared, set by SIGINT handler */
    struct qs_worker_result result;               /* filled in before thread exits */
};

#endif /* QS_SIEGE_H */

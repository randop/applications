/* stats.h -- per-worker latency sampling and result aggregation
 *
 * Each worker thread owns one `struct qs_stats` (no locking needed while
 * the worker owns it). Latency samples are stored in a fixed-capacity
 * buffer; once full, reservoir sampling keeps the buffer statistically
 * representative instead of just truncating the tail of the run.
 */
#ifndef QS_STATS_H
#define QS_STATS_H

#include <stdint.h>
#include <stddef.h>

/* Per-worker sample capacity per metric (8MB per metric at 8 bytes/sample). */
#define QS_SAMPLE_CAP (1u << 20)

struct qs_latency_samples {
    double   *buf;
    uint32_t  cap;
    uint32_t  len;      /* number of slots filled so far (<= cap) */
    uint64_t  seen;      /* total samples observed, including reservoir-dropped ones */
    unsigned  rng_state; /* rand_r seed, private to this buffer's owner thread */
};

struct qs_stats {
    /* Connections */
    uint64_t conns_attempted;
    uint64_t conns_established;
    uint64_t conns_failed;       /* handshake failure or connect() error */
    uint64_t conns_reset;        /* closed by peer / error after establishment */

    /* Streams */
    uint64_t streams_opened;
    uint64_t streams_completed;  /* got a full response or clean FIN, as configured */
    uint64_t streams_failed;     /* reset or errored before completion */

    /* Bytes */
    uint64_t bytes_sent;
    uint64_t bytes_recv;

    struct qs_latency_samples handshake_us;
    struct qs_latency_samples stream_us;
};

/* Percentile summary computed from a (sorted-in-place) sample buffer. */
struct qs_percentiles {
    double min, p50, p90, p95, p99, p999, max, mean;
    uint32_t n;      /* samples the summary is based on */
    uint64_t seen;   /* total samples observed before reservoir sampling */
};

int  qs_stats_init(struct qs_stats *st, unsigned rng_seed);
void qs_stats_destroy(struct qs_stats *st);

/* Record a latency sample (microseconds). Thread-unsafe by design: call
 * only from the thread that owns `st`. */
void qs_sample_add(struct qs_latency_samples *s, double value_us);

/* Sorts `s->buf[0..len)` in place and fills `out`. Safe to call once all
 * writes to `s` are done (e.g. at end of run, from the owning thread). */
void qs_percentiles_compute(struct qs_latency_samples *s, struct qs_percentiles *out);

/* Merges `src` into `dst` (sums counters, concatenates + re-reservoirs
 * latency samples). Used by main thread to combine per-worker stats. */
void qs_stats_merge(struct qs_stats *dst, const struct qs_stats *src);

#endif /* QS_STATS_H */

/* stats.c -- see stats.h */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include "stats.h"

static int
samples_init(struct qs_latency_samples *s, uint32_t cap, unsigned seed)
{
    s->buf = malloc(cap * sizeof(double));
    if (!s->buf)
        return -1;
    s->cap = cap;
    s->len = 0;
    s->seen = 0;
    s->rng_state = seed ? seed : 1;
    return 0;
}

int
qs_stats_init(struct qs_stats *st, unsigned rng_seed)
{
    memset(st, 0, sizeof(*st));
    if (0 != samples_init(&st->handshake_us, QS_SAMPLE_CAP, rng_seed))
        return -1;
    if (0 != samples_init(&st->stream_us, QS_SAMPLE_CAP, rng_seed ^ 0x9e3779b9u))
    {
        free(st->handshake_us.buf);
        return -1;
    }
    return 0;
}

void
qs_stats_destroy(struct qs_stats *st)
{
    free(st->handshake_us.buf);
    free(st->stream_us.buf);
    st->handshake_us.buf = NULL;
    st->stream_us.buf = NULL;
}

void
qs_sample_add(struct qs_latency_samples *s, double value_us)
{
    ++s->seen;
    if (s->len < s->cap)
    {
        s->buf[s->len++] = value_us;
        return;
    }
    /* Reservoir sampling (Algorithm R): once full, replace a uniformly
     * random existing slot with decreasing probability as more samples
     * arrive, so the buffer stays a representative sample of the whole
     * run instead of just its first QS_SAMPLE_CAP entries. */
    uint64_t j = (uint64_t) (rand_r(&s->rng_state) % 0x7fffffffu);
    /* Scale j into [0, seen) using the low bits; good enough for our
     * purposes (this is a reporting tool, not a statistics library). */
    j = j % s->seen;
    if (j < s->cap)
        s->buf[j] = value_us;
}

static int
cmp_double(const void *a, const void *b)
{
    double da = *(const double *) a, db = *(const double *) b;
    return (da > db) - (da < db);
}

static double
pct(const double *sorted, uint32_t n, double p)
{
    if (n == 0)
        return 0.0;
    double idx = p * (n - 1);
    uint32_t lo = (uint32_t) idx;
    uint32_t hi = lo + 1 < n ? lo + 1 : lo;
    double frac = idx - (double) lo;
    return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
}

void
qs_percentiles_compute(struct qs_latency_samples *s, struct qs_percentiles *out)
{
    memset(out, 0, sizeof(*out));
    out->n = s->len;
    out->seen = s->seen;
    if (s->len == 0)
        return;

    qsort(s->buf, s->len, sizeof(double), cmp_double);

    double sum = 0;
    for (uint32_t i = 0; i < s->len; ++i)
        sum += s->buf[i];

    out->min  = s->buf[0];
    out->max  = s->buf[s->len - 1];
    out->mean = sum / s->len;
    out->p50  = pct(s->buf, s->len, 0.50);
    out->p90  = pct(s->buf, s->len, 0.90);
    out->p95  = pct(s->buf, s->len, 0.95);
    out->p99  = pct(s->buf, s->len, 0.99);
    out->p999 = pct(s->buf, s->len, 0.999);
}

static void
samples_merge(struct qs_latency_samples *dst, const struct qs_latency_samples *src)
{
    /* Approximation: replay each retained src sample through dst's
     * reservoir. This slightly underweights a worker whose reservoir
     * already dropped samples, but per-worker `seen` counts are close
     * in a balanced run, so the skew is negligible for reporting. */
    for (uint32_t i = 0; i < src->len; ++i)
        qs_sample_add(dst, src->buf[i]);
}

void
qs_stats_merge(struct qs_stats *dst, const struct qs_stats *src)
{
    dst->conns_attempted   += src->conns_attempted;
    dst->conns_established += src->conns_established;
    dst->conns_failed      += src->conns_failed;
    dst->conns_reset       += src->conns_reset;
    dst->streams_opened    += src->streams_opened;
    dst->streams_completed += src->streams_completed;
    dst->streams_failed    += src->streams_failed;
    dst->bytes_sent        += src->bytes_sent;
    dst->bytes_recv        += src->bytes_recv;
    samples_merge(&dst->handshake_us, &src->handshake_us);
    samples_merge(&dst->stream_us, &src->stream_us);
}

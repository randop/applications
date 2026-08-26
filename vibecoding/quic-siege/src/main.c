/* main.c -- quic-siege: a raw-QUIC connection/stream load generator
 * built on liblsquic. See README.md for build instructions and usage.
 */
#define _GNU_SOURCE
#include <getopt.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lsquic.h"
#include "siege.h"

static volatile sig_atomic_t g_stop = 0;

static void
on_sigint(int signo)
{
    (void) signo;
    g_stop = 1;
}

static void
usage(const char *argv0)
{
    fprintf(stderr,
"Usage: %s -H HOST -p PORT (-n COUNT | -t SECONDS) [options]\n"
"\n"
"Required (pick exactly one stop condition):\n"
"  -H, --host HOST          target host or IP\n"
"  -p, --port PORT          target UDP port\n"
"  -n, --connections N      stop after N total connection attempts\n"
"  -t, --duration SECS      stop after SECS seconds of sustained load\n"
"\n"
"Options:\n"
"  -A, --alpn STRING        ALPN token to negotiate (default: raw)\n"
"      --sni NAME           SNI/hostname sent in ClientHello (default: HOST)\n"
"  -c, --concurrency N      max simultaneous open connections, total (default: 50)\n"
"  -T, --threads N          worker threads (default: nproc)\n"
"  -r, --rate N             connection attempts/sec, global, 0=unlimited (default: 0)\n"
"  -s, --streams N          streams per connection (default: 1)\n"
"  -b, --payload BYTES      payload bytes written per stream (default: 1024)\n"
"  -w, --wait-response      wait for a response + EOF on each stream before\n"
"                           closing it, and measure that as stream latency\n"
"                           (default: fire-and-forget -- write, then close)\n"
"      --idle-timeout SECS  QUIC idle timeout (default: 30)\n"
"      --handshake-timeout MS  handshake timeout in ms (default: 10000)\n"
"  -g, --drain-timeout SECS grace period for in-flight streams at end of run\n"
"                           before force-closing stragglers (default: 10)\n"
"      --verify-cert        verify the peer certificate against system CAs\n"
"                           (default: no verification, like most siege tools)\n"
"  -q, --quiet              suppress lsquic's own logging\n"
"  -h, --help                this help\n",
        argv0);
}

static double
now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec * 1e6 + (double) ts.tv_nsec / 1e3;
}

static void
print_pct_line(const char *label, struct qs_percentiles *p)
{
    if (p->n == 0)
    {
        printf("  %-24s (no samples)\n", label);
        return;
    }
    printf("  %-24s min %8.0f  p50 %8.0f  p90 %8.0f  p95 %8.0f  p99 %8.0f  p999 %8.0f  max %8.0f  mean %8.0f  (n=%u of %llu seen)\n",
        label, p->min, p->p50, p->p90, p->p95, p->p99, p->p999, p->max, p->mean,
        p->n, (unsigned long long) p->seen);
}

int
main(int argc, char **argv)
{
    struct qs_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.alpn = "raw";
    cfg.concurrency = 50;
    cfg.n_threads = 0; /* resolved below */
    cfg.mode = QS_MODE_COUNT;
    cfg.n_connections = 0;
    cfg.duration_s = 0;
    cfg.streams_per_conn = 1;
    cfg.payload_size = 1024;
    cfg.wait_response = 0;
    cfg.rate = 0;
    cfg.idle_timeout_s = 30;
    cfg.handshake_timeout_ms = 10000;
    cfg.drain_timeout_s = 10;
    cfg.quiet = 0;
    cfg.verify_cert = 0;

    int have_n = 0, have_t = 0;

    enum { OPT_SNI = 1000, OPT_IDLE_TO, OPT_HSK_TO, OPT_VERIFY };
    static const struct option longopts[] = {
        {"host",             required_argument, 0, 'H'},
        {"port",             required_argument, 0, 'p'},
        {"alpn",             required_argument, 0, 'A'},
        {"sni",              required_argument, 0, OPT_SNI},
        {"concurrency",      required_argument, 0, 'c'},
        {"threads",          required_argument, 0, 'T'},
        {"rate",             required_argument, 0, 'r'},
        {"connections",      required_argument, 0, 'n'},
        {"duration",         required_argument, 0, 't'},
        {"streams",          required_argument, 0, 's'},
        {"payload",          required_argument, 0, 'b'},
        {"wait-response",    no_argument,       0, 'w'},
        {"idle-timeout",     required_argument, 0, OPT_IDLE_TO},
        {"handshake-timeout",required_argument, 0, OPT_HSK_TO},
        {"drain-timeout",    required_argument, 0, 'g'},
        {"verify-cert",      no_argument,       0, OPT_VERIFY},
        {"quiet",            no_argument,       0, 'q'},
        {"help",             no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "H:p:A:c:T:r:n:t:s:b:wg:qh", longopts, NULL)) != -1)
    {
        switch (opt)
        {
        case 'H': cfg.host = optarg; break;
        case 'p': cfg.port = optarg; break;
        case 'A': cfg.alpn = optarg; break;
        case OPT_SNI: cfg.sni = optarg; break;
        case 'c': cfg.concurrency = (unsigned) strtoul(optarg, NULL, 10); break;
        case 'T': cfg.n_threads = (unsigned) strtoul(optarg, NULL, 10); break;
        case 'r': cfg.rate = strtod(optarg, NULL); break;
        case 'n': cfg.n_connections = strtoull(optarg, NULL, 10); have_n = 1; break;
        case 't': cfg.duration_s = strtod(optarg, NULL); have_t = 1; break;
        case 's': cfg.streams_per_conn = (unsigned) strtoul(optarg, NULL, 10); break;
        case 'b': cfg.payload_size = (unsigned) strtoul(optarg, NULL, 10); break;
        case 'w': cfg.wait_response = 1; break;
        case OPT_IDLE_TO: cfg.idle_timeout_s = (unsigned) strtoul(optarg, NULL, 10); break;
        case OPT_HSK_TO: cfg.handshake_timeout_ms = (unsigned) strtoul(optarg, NULL, 10); break;
        case 'g': cfg.drain_timeout_s = (unsigned) strtoul(optarg, NULL, 10); break;
        case OPT_VERIFY: cfg.verify_cert = 1; break;
        case 'q': cfg.quiet = 1; break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }

    if (!cfg.host || !cfg.port)
    {
        fprintf(stderr, "error: --host and --port are required\n\n");
        usage(argv[0]);
        return 1;
    }
    if (have_n == have_t)
    {
        fprintf(stderr, "error: specify exactly one of --connections or --duration\n\n");
        usage(argv[0]);
        return 1;
    }
    cfg.mode = have_n ? QS_MODE_COUNT : QS_MODE_DURATION;
    if (!cfg.sni)
        cfg.sni = cfg.host;
    if (cfg.n_threads == 0)
    {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        cfg.n_threads = n > 0 ? (unsigned) n : 4;
    }
    if (cfg.concurrency == 0)
        cfg.concurrency = cfg.n_threads;
    if (cfg.payload_size == 0 && cfg.wait_response)
        fprintf(stderr, "warning: --payload 0 with --wait-response: streams will wait "
                         "for a response to an empty write\n");

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    int gai = getaddrinfo(cfg.host, cfg.port, &hints, &cfg.resolved);
    if (gai != 0)
    {
        fprintf(stderr, "error: could not resolve %s:%s: %s\n",
            cfg.host, cfg.port, gai_strerror(gai));
        return 1;
    }

    if (!cfg.quiet)
        printf("quic-siege: %s:%s  alpn=\"%s\"  sni=%s  mode=%s  concurrency=%u  threads=%u\n",
            cfg.host, cfg.port, cfg.alpn, cfg.sni,
            cfg.mode == QS_MODE_COUNT ? "count" : "duration",
            cfg.concurrency, cfg.n_threads);

    if (0 != qs_worker_global_init(&cfg))
    {
        fprintf(stderr, "error: global init failed (lsquic_global_init / payload buffer / SSL_CTX)\n");
        freeaddrinfo(cfg.resolved);
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    pthread_t *tids = calloc(cfg.n_threads, sizeof(*tids));
    struct qs_worker_args *wargs = calloc(cfg.n_threads, sizeof(*wargs));

    uint64_t base = cfg.n_connections / cfg.n_threads;
    uint64_t rem  = cfg.n_connections % cfg.n_threads;

    double t_start = now_us();
    for (unsigned i = 0; i < cfg.n_threads; ++i)
    {
        wargs[i].cfg = &cfg;
        wargs[i].worker_id = i;
        wargs[i].stop_flag = &g_stop;
        wargs[i].n_connections_share = base + (i < rem ? 1 : 0);
        pthread_create(&tids[i], NULL, qs_worker_run, &wargs[i]);
    }

    struct qs_stats total;
    qs_stats_init(&total, 1);
    int any_failed_setup = 0;
    for (unsigned i = 0; i < cfg.n_threads; ++i)
    {
        pthread_join(tids[i], NULL);
        if (!wargs[i].result.ok)
        {
            any_failed_setup = 1;
            continue;
        }
        qs_stats_merge(&total, &wargs[i].result.stats);
        qs_stats_destroy(&wargs[i].result.stats);
    }
    double t_end = now_us();
    double elapsed_s = (t_end - t_start) / 1e6;

    if (any_failed_setup)
        fprintf(stderr, "warning: one or more worker threads failed to initialize "
                         "(epoll/engine setup) -- results below are partial\n");

    struct qs_percentiles hsk_pct, stream_pct;
    qs_percentiles_compute(&total.handshake_us, &hsk_pct);
    qs_percentiles_compute(&total.stream_us, &stream_pct);

    printf("\n=== quic-siege results (%.2fs) ===\n", elapsed_s);
    printf("Connections:  attempted %llu   established %llu (%.2f%%)   failed %llu   reset %llu\n",
        (unsigned long long) total.conns_attempted,
        (unsigned long long) total.conns_established,
        total.conns_attempted ? 100.0 * (double) total.conns_established / (double) total.conns_attempted : 0.0,
        (unsigned long long) total.conns_failed,
        (unsigned long long) total.conns_reset);
    print_pct_line("Handshake latency (us):", &hsk_pct);
    printf("Streams:      opened %llu   completed %llu   failed %llu\n",
        (unsigned long long) total.streams_opened,
        (unsigned long long) total.streams_completed,
        (unsigned long long) total.streams_failed);
    if (cfg.wait_response)
        print_pct_line("Stream latency (us):", &stream_pct);
    printf("Throughput:   sent %.2f MB   recv %.2f MB   =>  %.1f conns/s   %.1f streams/s\n",
        (double) total.bytes_sent / 1e6, (double) total.bytes_recv / 1e6,
        elapsed_s > 0 ? (double) total.conns_established / elapsed_s : 0.0,
        elapsed_s > 0 ? (double) total.streams_completed / elapsed_s : 0.0);

    qs_stats_destroy(&total);
    qs_worker_global_cleanup();
    freeaddrinfo(cfg.resolved);
    free(tids);
    free(wargs);
    return 0;
}

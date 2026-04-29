#include <lsquic.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <math.h>  // For completeness (LSQUIC internals)

#define BUFSZ 1024
#define PORT_STR "4433"  // Default port as string for settings

// Forward declarations
static lsquic_conn_ctx_t *on_new_conn(void *ctx, lsquic_conn_t *conn);
static void on_conn_closed(lsquic_conn_t *conn);
static lsquic_stream_ctx_t *on_new_stream(void *ctx, lsquic_stream_t *stream);
static void on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx);
static void on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx);
static void on_close(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx);
static int packets_out(void *ctx, const struct lsquic_out_spec *specs, unsigned n_specs);
static SSL_CTX *get_ssl_ctx(void *ctx);

int log_to_stdout(void *ctx, const char *buf, size_t len) {
  fwrite(buf, 1, len, stdout);
  fflush(stdout);
  return 0;
}

// Stream interface
static const struct lsquic_stream_if stream_if = {
    .on_new_conn    = on_new_conn,
    .on_conn_closed = on_conn_closed,
    .on_new_stream  = on_new_stream,
    .on_read        = on_read,
    .on_write       = on_write,
    .on_close       = on_close,
};

// Engine API
static struct lsquic_engine_api engine_api = {
    .ea_packets_out     = packets_out,
    .ea_stream_if       = &stream_if,
    .ea_get_ssl_ctx     = get_ssl_ctx,
};

// Per-stream context
struct stream_ctx {
    lsquic_stream_t *stream;
    char buf[BUFSZ];
    size_t bufsz;
    size_t off;
    bool read_eof;
};

// Per-conn context (simple, no extra data needed)
struct conn_ctx {
    // Empty for basic echo
};

// Global engine and socket
static lsquic_engine_t *engine;
static int sockfd;
static struct event *read_ev, *timer_ev;
static struct event_base *base;
static struct sockaddr_in local_sa;

// Timer callback for processing connections
static void timer_cb(evutil_socket_t fd, short events, void *arg) {
    printf("[DEBUG] Timer callback: Processing connections\n");
    lsquic_engine_process_conns(engine);
    int diff;
    if (lsquic_engine_earliest_adv_tick(engine, &diff)) {
        struct timeval tv = {0, diff > 0 ? diff : 0};
        event_add(timer_ev, &tv);
        printf("[DEBUG] Timer rescheduled in %d us\n", diff);
    }
}

// Read callback for incoming UDP packets
static void read_cb(evutil_socket_t fd, short events, void *arg) {
    printf("[DEBUG] Read callback: Received UDP packet\n");
    struct sockaddr_storage peer_sa;
    socklen_t peer_len = sizeof(peer_sa);
    unsigned char buf[BUFSZ];
    ssize_t sz = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&peer_sa, &peer_len);
    if (sz > 0) {
        char peer_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &((struct sockaddr_in *)&peer_sa)->sin_addr, peer_ip, sizeof(peer_ip));
        printf("[DEBUG] Packet in: %zd bytes from %s\n", sz, peer_ip);
        lsquic_engine_packet_in(engine, buf, (size_t)sz, (struct sockaddr *)&local_sa, (struct sockaddr *)&peer_sa, NULL, 0);
    } else if (sz < 0) {
        printf("[DEBUG] recvfrom error: %s\n", strerror(errno));
    }
    lsquic_engine_process_conns(engine);  // Process after input
}

// Packets out callback
static int packets_out(void *ctx, const struct lsquic_out_spec *specs, unsigned n_specs) {
    int fd = (int)(uintptr_t)ctx;
    unsigned n;
    for (n = 0; n < n_specs; ++n) {
        struct msghdr msg = {0};
        msg.msg_name = (void *)specs[n].dest_sa;
        msg.msg_namelen = sizeof(struct sockaddr_in);
        msg.msg_iov = specs[n].iov;
        msg.msg_iovlen = specs[n].iovlen;
        ssize_t sent = sendmsg(fd, &msg, 0);
        if (sent < 0) {
            printf("[DEBUG] sendmsg error for spec %u: %s\n", n, strerror(errno));
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                lsquic_engine_send_unsent_packets(engine);
            }
            break;
        } else {
            printf("[DEBUG] Sent %zd bytes in spec %u\n", sent, n);
        }
    }
    return (int)n;
}

// Load X.509 PEM keys (hardcoded paths)
static SSL_CTX *get_ssl_ctx(void *ctx) {
    static SSL_CTX *ssl_ctx = NULL;
    if (!ssl_ctx) {
        printf("[DEBUG] Creating server SSL_CTX and loading cert.pem/key.pem\n");
        ssl_ctx = SSL_CTX_new(TLS_method());
        if (!ssl_ctx) {
            fprintf(stderr, "Failed to create SSL_CTX\n");
            exit(EXIT_FAILURE);
        }

      SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION);
      SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);

        // Load certificate (X.509 PEM)
        if (SSL_CTX_use_certificate_file(ssl_ctx, "cert.pem", SSL_FILETYPE_PEM) <= 0) {
            ERR_print_errors_fp(stderr);
            fprintf(stderr, "Failed to load cert.pem\n");
            exit(EXIT_FAILURE);
        }
        printf("[DEBUG] cert.pem loaded successfully\n");
        // Load private key (PEM)
        if (SSL_CTX_use_PrivateKey_file(ssl_ctx, "key.pem", SSL_FILETYPE_PEM) <= 0) {
            ERR_print_errors_fp(stderr);
            fprintf(stderr, "Failed to load key.pem\n");
            exit(EXIT_FAILURE);
        }
        printf("[DEBUG] key.pem loaded successfully\n");
        if (!SSL_CTX_check_private_key(ssl_ctx)) {
            fprintf(stderr, "Private key does not match public cert\n");
            exit(EXIT_FAILURE);
        }
        printf("[DEBUG] Private key matches cert\n");
    }
    return ssl_ctx;
}

// Connection callbacks
static lsquic_conn_ctx_t *on_new_conn(void *ctx, lsquic_conn_t *conn) {
    printf("[DEBUG] New connection callback: conn %p\n", (void *)conn);
    fflush(stdout);
    lsquic_conn_ctx_t *cctx = calloc(1, sizeof(struct conn_ctx));
    return cctx;
}

static void on_conn_closed(lsquic_conn_t *conn) {
    printf("[DEBUG] Connection closed callback: conn %p\n", (void *)conn);
    fflush(stdout);
    lsquic_conn_ctx_t *cctx = lsquic_conn_get_ctx(conn);
    free(cctx);
}

// Stream callbacks
static lsquic_stream_ctx_t *on_new_stream(void *ctx, lsquic_stream_t *stream) {
    printf("[DEBUG] New stream callback: stream %p\n", (void *)stream);
    fflush(stdout);
    struct stream_ctx *stctx = calloc(1, sizeof(struct stream_ctx));
    stctx->stream = stream;
    lsquic_stream_wantread(stream, 1);
    return stctx;
}

static void on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx_) {
    struct stream_ctx *stctx = (struct stream_ctx *)stctx_;
    printf("[DEBUG] Read callback on stream %p, offset %zu\n", (void *)stream, stctx->off);
    ssize_t nr;
    while ((nr = lsquic_stream_read(stream, stctx->buf + stctx->off, BUFSZ - stctx->off)) > 0) {
        stctx->off += nr;
        printf("[DEBUG] Read %zd bytes on stream %p (total %zu)\n", nr, (void *)stream, stctx->off);
    }
    if (nr == 0) {  // EOF
        stctx->read_eof = true;
        printf("[DEBUG] EOF on stream %p, received %zu bytes: %.*s\n", (void *)stream, stctx->off, (int)stctx->off, stctx->buf);
        lsquic_stream_shutdown(stream, SHUT_RD);
        lsquic_stream_wantwrite(stream, 1);
        // Reverse the buffer for echo
        for (size_t i = 0; i < stctx->off / 2; ++i) {
            char tmp = stctx->buf[i];
            stctx->buf[i] = stctx->buf[stctx->off - 1 - i];
            stctx->buf[stctx->off - 1 - i] = tmp;
        }
        printf("[DEBUG] Reversed message on stream %p: %.*s\n", (void *)stream, (int)stctx->off, stctx->buf);
        stctx->bufsz = stctx->off;
        stctx->off = 0;
    } else if (nr < 0 && errno != EAGAIN) {
        fprintf(stderr, "[DEBUG] Read error on stream %p: %s\n", (void *)stream, strerror(errno));
        lsquic_stream_close(stream);
    }
}

static void on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx_) {
    struct stream_ctx *stctx = (struct stream_ctx *)stctx_;
    printf("[DEBUG] Write callback on stream %p\n", (void *)stream);
    ssize_t nw = lsquic_stream_write(stream, stctx->buf + stctx->off, stctx->bufsz - stctx->off);
    if (nw > 0) {
        stctx->off += nw;
        printf("[DEBUG] Wrote %zd bytes on stream %p (total %zu/%zu)\n", nw, (void *)stream, stctx->off, stctx->bufsz);
    }
    if (stctx->off == stctx->bufsz) {
        printf("[DEBUG] Fully echoed on stream %p, closing\n", (void *)stream);
        lsquic_stream_close(stream);
    } else if (nw < 0 && errno != EAGAIN) {
        fprintf(stderr, "[DEBUG] Write error on stream %p: %s\n", (void *)stream, strerror(errno));
        lsquic_stream_close(stream);
    }
}

static void on_close(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx_) {
    struct stream_ctx *stctx = (struct stream_ctx *)stctx_;
    printf("[DEBUG] Stream close callback: stream %p\n", (void *)stream);
    free(stctx);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("[DEBUG] Starting server on %s:%s\n", argv[1], argv[2]);

    struct lsquic_logger_if logger_if = {
      .log_buf = log_to_stdout
    };

    lsquic_logger_init(&logger_if, NULL, LLTS_HHMMSSUS);

  if (lsquic_set_log_level("debug") != 0) {
    fprintf(stderr, "Failed to set log level\n");
    // Handle error
  }


    printf("[DEBUG] Global server init\n");
    if (lsquic_global_init(LSQUIC_GLOBAL_SERVER) != 0) {
        fprintf(stderr, "Global init failed\n");
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Global init successful\n");

    // Create UDP socket
    printf("[DEBUG] Creating UDP socket\n");
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Socket created: fd %d\n", sockfd);
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind
    memset(&local_sa, 0, sizeof(local_sa));
    local_sa.sin_family = AF_INET;
    local_sa.sin_addr.s_addr = inet_addr(argv[1]);
    local_sa.sin_port = htons(atoi(argv[2]));
    if (bind(sockfd, (struct sockaddr *)&local_sa, sizeof(local_sa)) < 0) {
        perror("bind");
        close(sockfd);
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Bound to %s:%s\n", argv[1], argv[2]);

    // Engine settings
    printf("[DEBUG] Initializing engine settings\n");
    struct lsquic_engine_settings settings;
    lsquic_engine_init_settings(&settings, LSENG_SERVER);
    settings.es_versions = LSQUIC_SUPPORTED_VERSIONS;
    char errbuf[256];
    if (lsquic_engine_check_settings(&settings, LSENG_SERVER, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "Settings error: %s\n", errbuf);
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Engine settings valid\n");
    engine_api.ea_settings = &settings;

    // Create engine
    printf("[DEBUG] Creating engine\n");
    engine = lsquic_engine_new(LSENG_SERVER, &engine_api);
    if (!engine) {
        fprintf(stderr, "Engine new failed\n");
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Engine created: %p\n", (void *)engine);
    engine_api.ea_packets_out_ctx = (void *)(uintptr_t)sockfd;

    // Event loop setup
    printf("[DEBUG] Setting up event loop\n");
    base = event_base_new();
    read_ev = event_new(base, sockfd, EV_READ | EV_PERSIST, read_cb, NULL);
    event_add(read_ev, NULL);
    timer_ev = event_new(base, -1, EV_PERSIST, timer_cb, NULL);
    struct timeval tv = {0, 0};
    event_add(timer_ev, &tv);

    printf("Server listening on %s:%s\n", argv[1], argv[2]);
    event_base_dispatch(base);
    printf("[DEBUG] Event loop exited\n");

    printf("[DEBUG] Cleaning up\n");
    lsquic_engine_destroy(engine);
    lsquic_global_cleanup();
    event_base_free(base);
    close(sockfd);
    return EXIT_SUCCESS;
}
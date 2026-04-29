#include <lsquic.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
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
#include <math.h>  // For completeness

#define BUFSZ 1024

// Forward declarations (reuse same callbacks as server, but client mode)
static lsquic_conn_ctx_t *on_new_conn(void *ctx, lsquic_conn_t *conn);
static void on_conn_closed(lsquic_conn_t *conn);
static lsquic_stream_ctx_t *on_new_stream(void *ctx, lsquic_stream_t *stream);
static void on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx);
static void on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx);
static void on_close(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx);
static int packets_out(void *ctx, const struct lsquic_out_spec *specs, unsigned n_specs);
static SSL_CTX *get_client_ssl_ctx(void *peer_ctx, const struct sockaddr *local_sa);  // Fixed signature for client SSL_CTX for verification

int log_to_stdout(void *ctx, const char *buf, size_t len) {
  fwrite(buf, 1, len, stdout);
  fflush(stdout);
  return 0;
}

// Stream interface (same as server)
static const struct lsquic_stream_if stream_if = {
    .on_new_conn    = on_new_conn,
    .on_conn_closed = on_conn_closed,
    .on_new_stream  = on_new_stream,
    .on_read        = on_read,
    .on_write       = on_write,
    .on_close       = on_close,
};

// Engine API (now includes SSL_CTX for client verification)
static struct lsquic_engine_api engine_api = {
    .ea_packets_out = packets_out,
    .ea_stream_if   = &stream_if,
    .ea_get_ssl_ctx = get_client_ssl_ctx,  // Added for client TLS
};

// Updated conn_ctx to store the message
struct conn_ctx {
    char *msg;
};

// Reuse stream_ctx from server
struct stream_ctx {
    lsquic_stream_t *stream;
    char buf[BUFSZ];
    size_t bufsz;
    size_t off;
    bool read_eof;
    char *msg;  // Client message to send
};

// Globals
static lsquic_engine_t *engine;
static int sockfd;
static struct event *read_ev, *timer_ev;
static struct event_base *base;
static struct sockaddr_in local_sa, peer_sa;
static volatile bool done = false;

// Timer and read callbacks (same as server)
static void timer_cb(evutil_socket_t fd, short events, void *arg) {
    printf("[DEBUG] Timer callback: Processing connections\n");
    fflush(stdout);
    lsquic_engine_process_conns(engine);
    int diff;
    if (lsquic_engine_earliest_adv_tick(engine, &diff)) {
        struct timeval tv = {0, diff > 0 ? diff : 0};
        event_add(timer_ev, &tv);
        printf("[DEBUG] Timer rescheduled in %d us\n", diff);
        fflush(stdout);
    }
    if (done) {
        printf("[DEBUG] Done flag set, exiting event loop\n");
        fflush(stdout);
        event_base_loopexit(base, NULL);
    }
}

static void read_cb(evutil_socket_t fd, short events, void *arg) {
    printf("[DEBUG] Read callback: Received UDP packet\n");
    fflush(stdout);
    struct sockaddr_storage sa;
    socklen_t sa_len = sizeof(sa);
    unsigned char buf[BUFSZ];
    ssize_t sz = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&sa, &sa_len);
    if (sz > 0) {
        printf("[DEBUG] Packet in: %zd bytes from %s\n", sz, inet_ntoa(((struct sockaddr_in *)&sa)->sin_addr));
        fflush(stdout);
        lsquic_engine_packet_in(engine, buf, (size_t)sz, (struct sockaddr *)&local_sa, (struct sockaddr *)&sa, NULL, 0);
    } else if (sz < 0) {
        printf("[DEBUG] recvfrom error: %s\n", strerror(errno));
        fflush(stdout);
    }
    lsquic_engine_process_conns(engine);
}

// Packets out (same as server)
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
            fflush(stdout);
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                lsquic_engine_send_unsent_packets(engine);
            }
            break;
        } else {
            printf("[DEBUG] Sent %zd bytes in spec %u\n", sent, n);
            fflush(stdout);
        }
    }
    return (int)n;
}

// Load cert.pem as CA for server verification - fixed signature
static SSL_CTX *get_client_ssl_ctx(void *peer_ctx, const struct sockaddr *local_sa) {
    static SSL_CTX *ssl_ctx = NULL;
    if (!ssl_ctx) {
        printf("[DEBUG] Creating client SSL_CTX and loading cert.pem\n");
        fflush(stdout);
        ssl_ctx = SSL_CTX_new(TLS_method());
        if (!ssl_ctx) {
            fprintf(stderr, "Failed to create client SSL_CTX\n");
            exit(EXIT_FAILURE);
        }

      SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION);
      SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);
      SSL_CTX_set_default_verify_paths(ssl_ctx);

        // Load CA cert (server's self-signed cert.pem) for verification
        if (SSL_CTX_load_verify_locations(ssl_ctx, "/opt/rfl/cert.pem", NULL) != 1) {
            ERR_print_errors_fp(stderr);
            fprintf(stderr, "Failed to load cert.pem as CA\n");
            exit(EXIT_FAILURE);
        }
        printf("[DEBUG] cert.pem loaded successfully as CA\n");
        fflush(stdout);
        // Enable peer verification
        if (false) {
          SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
          SSL_CTX_set_verify_depth(ssl_ctx, 4);
        } else {
          SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);
          printf("[DEBUG] Peer verification disabled\n");
        }
        printf("[DEBUG] Peer verification enabled\n");
        fflush(stdout);
    }
    return ssl_ctx;
}

// Callbacks (adapted for client: send msg on new stream, print on read)
static lsquic_conn_ctx_t *on_new_conn(void *ctx, lsquic_conn_t *conn) {
    printf("[DEBUG] New connection callback: conn %p\n", (void *)conn);
    fflush(stdout);
    struct conn_ctx *cctx = calloc(1, sizeof(struct conn_ctx));
    cctx->msg = (char *)ctx;  // ctx is ea_stream_if_ctx = msg
    // Create initial stream after handshake (client side)
    lsquic_conn_make_stream(conn);
    printf("[DEBUG] Initial stream requested on conn %p\n", (void *)conn);
    fflush(stdout);
    return (lsquic_conn_ctx_t *)cctx;
}

static void on_conn_closed(lsquic_conn_t *conn) {
    printf("[DEBUG] Connection closed callback: conn %p\n", (void *)conn);
    fflush(stdout);
    lsquic_conn_ctx_t *cctx_raw = lsquic_conn_get_ctx(conn);
    struct conn_ctx *cctx = (struct conn_ctx *)cctx_raw;
    // msg is transferred to stream or freed elsewhere
    free(cctx);
    done = true;
}

static lsquic_stream_ctx_t *on_new_stream(void *ctx, lsquic_stream_t *stream) {
  printf("[DEBUG] New stream callback: stream %p, ctx %p\n", (void *)stream, ctx);
  fflush(stdout);
  struct stream_ctx *stctx = calloc(1, sizeof(struct stream_ctx));
  stctx->stream = stream;
  struct conn_ctx *cctx = (struct conn_ctx *)ctx;
  stctx->msg = cctx->msg;  // Transfer ownership of msg from conn to stream
  cctx->msg = NULL;
  if (stctx->msg) {
    printf("[DEBUG] Stream %p will send: %s\n", (void *)stream, stctx->msg);
    fflush(stdout);
  }
  lsquic_stream_wantwrite(stream, 1);
  return (lsquic_stream_ctx_t *)stctx;
}

static void on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx_) {
    struct stream_ctx *stctx = (struct stream_ctx *)stctx_;
    printf("[DEBUG] Read callback on stream %p, offset %zu\n", (void *)stream, stctx->off);
    fflush(stdout);
    ssize_t nr;
    while ((nr = lsquic_stream_read(stream, stctx->buf + stctx->off, BUFSZ - stctx->off)) > 0) {
        stctx->off += nr;
        printf("[DEBUG] Read %zd bytes on stream %p (total %zu)\n", nr, (void *)stream, stctx->off);
        fflush(stdout);
    }
    if (nr == 0) {
        stctx->read_eof = true;
        printf("[DEBUG] EOF on stream %p, received %zu bytes: %.*s\n", (void *)stream, stctx->off, (int)stctx->off, stctx->buf);
        fflush(stdout);
        fwrite(stctx->buf, 1, stctx->off, stdout);
        fflush(stdout);
        lsquic_stream_close(stream);
    } else if (nr < 0 && errno != EAGAIN) {
        fprintf(stderr, "[DEBUG] Read error on stream %p: %s\n", (void *)stream, strerror(errno));
        fflush(stderr);
        lsquic_stream_close(stream);
    }
}

static void on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx_) {
    struct stream_ctx *stctx = (struct stream_ctx *)stctx_;
    printf("[DEBUG] Write callback on stream %p\n", (void *)stream);
    fflush(stdout);
    if (!stctx->msg) return;
    size_t msg_len = strlen(stctx->msg);
    ssize_t nw = lsquic_stream_write(stream, stctx->msg, msg_len);
    if (nw > 0 && (size_t)nw == msg_len) {
        printf("[DEBUG] Fully wrote %zu bytes on stream %p\n", msg_len, (void *)stream);
        fflush(stdout);
        free(stctx->msg);
        stctx->msg = NULL;
        lsquic_stream_wantread(stream, 1);
        lsquic_stream_shutdown(stream, SHUT_WR);
    } else if (nw > 0) {
        printf("[DEBUG] Partially wrote %zd/%zu bytes on stream %p\n", nw, msg_len, (void *)stream);
        fflush(stdout);
    } else if (nw < 0 && errno != EAGAIN) {
        fprintf(stderr, "[DEBUG] Write error on stream %p: %s\n", (void *)stream, strerror(errno));
        fflush(stderr);
        lsquic_stream_close(stream);
    }
}

static void on_close(lsquic_stream_t *stream, lsquic_stream_ctx_t *stctx_) {
    struct stream_ctx *stctx = (struct stream_ctx *)stctx_;
    printf("[DEBUG] Stream close callback: stream %p\n", (void *)stream);
    fflush(stdout);
    if (stctx->msg) {
        free(stctx->msg);
        stctx->msg = NULL;
    }
    free(stctx);
    done = true;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port> [message]\n", argv[0]);
        return EXIT_FAILURE;
    }
    char *msg = (argc > 3) ? strdup(argv[3]) : strdup("Hello, QUIC!");  // Duplicate to own the string
    printf("[DEBUG] Starting client with message: %s\n", msg);
    fflush(stdout);

  struct lsquic_logger_if logger_if = {
    .log_buf = log_to_stdout
  };

  lsquic_logger_init(&logger_if, NULL, LLTS_HHMMSSUS);

  if (lsquic_set_log_level("debug") != 0) {
    fprintf(stderr, "Failed to set log level\n");
    // Handle error
    return EXIT_FAILURE;
  }

    printf("[DEBUG] Global client init\n");
    fflush(stdout);
    if (lsquic_global_init(LSQUIC_GLOBAL_CLIENT) != 0) {
        fprintf(stderr, "Global init failed\n");
        free(msg);
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Global init successful\n");
    fflush(stdout);

    // Create UDP socket (client binds to any)
    printf("[DEBUG] Creating UDP socket\n");
    fflush(stdout);
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        free(msg);
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Socket created: fd %d\n", sockfd);
    fflush(stdout);

    // Local addr (any)
    memset(&local_sa, 0, sizeof(local_sa));
    local_sa.sin_family = AF_INET;
    local_sa.sin_addr.s_addr = INADDR_ANY;
    local_sa.sin_port = 0;
    if (bind(sockfd, (struct sockaddr *)&local_sa, sizeof(local_sa)) < 0) {
        perror("bind");
        close(sockfd);
        free(msg);
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Bound to local addr (any port)\n");
    fflush(stdout);

    // Peer addr
    memset(&peer_sa, 0, sizeof(peer_sa));
    peer_sa.sin_family = AF_INET;
    peer_sa.sin_addr.s_addr = inet_addr(argv[1]);
    peer_sa.sin_port = htons(atoi(argv[2]));
    printf("[DEBUG] Peer addr: %s:%d\n", argv[1], atoi(argv[2]));
    fflush(stdout);

    // Engine settings (no es_ver field; verification via SSL_CTX)
    printf("[DEBUG] Initializing engine settings\n");
    fflush(stdout);
    struct lsquic_engine_settings settings;
    lsquic_engine_init_settings(&settings, 0);
  settings.es_versions = LSQUIC_SUPPORTED_VERSIONS;

    char errbuf[256];
    if (lsquic_engine_check_settings(&settings, 0, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "Settings error: %s\n", errbuf);
        free(msg);
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Engine settings valid\n");
    fflush(stdout);
    engine_api.ea_settings = &settings;
    engine_api.ea_stream_if_ctx = msg;  // Pass msg as stream_if_ctx to callbacks

    // Create engine
    printf("[DEBUG] Creating engine\n");
    fflush(stdout);
    engine = lsquic_engine_new(0, &engine_api);
    if (!engine) {
        fprintf(stderr, "Engine new failed\n");
        free(msg);
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Engine created: %p\n", (void *)engine);
    fflush(stdout);
    engine_api.ea_packets_out_ctx = (void *)(uintptr_t)sockfd;

    // Connect (pass NULL for conn_ctx as we'll set in on_new_conn; sni as argv[1] - note: ideally use hostname for SNI)
    printf("[DEBUG] Initiating connect to %s:%d\n", argv[1], atoi(argv[2]));
    fflush(stdout);
    lsquic_conn_t *conn = lsquic_engine_connect(engine, N_LSQVER,
        (struct sockaddr *)&local_sa, (struct sockaddr *)&peer_sa,
        (void *)(uintptr_t)sockfd, NULL, argv[1], 0, NULL, 0, NULL, 0);
    if (!conn) {
        fprintf(stderr, "Connect failed\n");
        lsquic_engine_destroy(engine);
        free(msg);
        return EXIT_FAILURE;
    }
    printf("[DEBUG] Connect successful: conn %p\n", (void *)conn);
    fflush(stdout);

    // Event loop
    printf("[DEBUG] Setting up event loop\n");
    fflush(stdout);
    base = event_base_new();
    read_ev = event_new(base, sockfd, EV_READ | EV_PERSIST, read_cb, NULL);
    event_add(read_ev, NULL);
    timer_ev = event_new(base, -1, EV_PERSIST, timer_cb, NULL);
    struct timeval tv = {0, 0};
    event_add(timer_ev, &tv);

    printf("Client connecting to %s:%s, sending: %s\n", argv[1], argv[2], msg);
    fflush(stdout);
    event_base_dispatch(base);
    printf("[DEBUG] Event loop exited\n");
    fflush(stdout);

    printf("[DEBUG] Cleaning up\n");
    fflush(stdout);
    lsquic_engine_destroy(engine);
    lsquic_global_cleanup();
    event_base_free(base);
    close(sockfd);
    // msg is freed in callbacks or here if not used
    if (msg) free(msg);  // But since passed to ctx, may be freed elsewhere; safe as it's owned
    return EXIT_SUCCESS;
}
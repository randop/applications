/* 
 * QUIC Echo Server with liburing (io_uring)
 *
 * Build:
 *   gcc -o server.bin main.c -llsquic -lssl -lcrypto -lz -luring -lpthread
 *
 * Run:
 *   ./server 0.0.0.0 4433 cert.pem key.pem
 */

#include <lsquic.h>
#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Buffer size for recv/send */
#define BUFFER_SIZE     (64 * 1024)
#define RING_QUEUE_DEPTH  512
#define MAX_CONNS         1024

/* Per-stream context */
struct stream_ctx {
    lsquic_stream_t *stream;
    char *buffer;
    size_t buf_len;
    size_t buf_used;
};

/* Per-connection context */
struct conn_ctx {
    /* Minimal for now */
};

static struct io_uring ring;
static int sockfd = -1;
static volatile int running = 1;

/* ====================== Packet Output ====================== */

static int
packets_out(void *ctx, const struct lsquic_out_spec *specs, unsigned n_specs)
{
    for (unsigned i = 0; i < n_specs; ++i) {
        const struct lsquic_out_spec *spec = &specs[i];

        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            /* Queue full - fall back or handle */
            continue;
        }

        struct msghdr msg = {0};
        msg.msg_name    = (void*)spec->peer_sa;
        msg.msg_namelen = spec->peer_sa_len;
        msg.msg_iov     = (struct iovec*)spec->iov;
        msg.msg_iovlen  = spec->iovlen;

        io_uring_prep_sendmsg(sqe, sockfd, &msg, 0);
        io_uring_sqe_set_data(sqe, NULL);   /* No user data needed for send */
    }

    io_uring_submit(&ring);
    return (int)n_specs;
}

/* ====================== Stream Callbacks ====================== */

static lsquic_conn_ctx_t *
on_new_conn(void *ctx, lsquic_conn_t *conn)
{
    struct conn_ctx *cctx = calloc(1, sizeof(*cctx));
    lsquic_conn_set_ctx(conn, cctx);
    printf("[+] New QUIC connection\n");
    return (lsquic_conn_ctx_t *)cctx;
}

static void
on_conn_closed(lsquic_conn_t *conn)
{
    struct conn_ctx *cctx = lsquic_conn_get_ctx(conn);
    if (cctx) free(cctx);
    printf("[-] Connection closed\n");
}

static lsquic_stream_ctx_t *
on_new_stream(void *ctx, lsquic_stream_t *stream)
{
    struct stream_ctx *sctx = calloc(1, sizeof(*sctx));
    sctx->stream = stream;
    lsquic_stream_set_ctx(stream, sctx);

    lsquic_stream_wantread(stream, 1);
    return (lsquic_stream_ctx_t *)sctx;
}

static void
on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *h)
{
    struct stream_ctx *sctx = (struct stream_ctx *)h;
    char tmp[8192];
    ssize_t n;

    while ((n = lsquic_stream_read(stream, tmp, sizeof(tmp))) > 0) {
        if (!sctx->buffer) {
            sctx->buf_len = 16384;
            sctx->buffer = malloc(sctx->buf_len);
        } else if (sctx->buf_used + n > sctx->buf_len) {
            sctx->buf_len *= 2;
            sctx->buffer = realloc(sctx->buffer, sctx->buf_len);
        }
        memcpy(sctx->buffer + sctx->buf_used, tmp, n);
        sctx->buf_used += n;
    }

    if (n == 0) {                       /* FIN received */
        lsquic_stream_wantread(stream, 0);
        if (sctx->buf_used > 0)
            lsquic_stream_wantwrite(stream, 1);
        else
            lsquic_stream_shutdown(stream, 1);
    } else if (n < 0) {
        lsquic_stream_close(stream);
    }
}

static void
on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *h)
{
    struct stream_ctx *sctx = (struct stream_ctx *)h;

    if (sctx->buf_used == 0) {
        lsquic_stream_wantwrite(stream, 0);
        return;
    }

    ssize_t nw = lsquic_stream_write(stream, sctx->buffer, sctx->buf_used);
    if (nw > 0) {
        memmove(sctx->buffer, sctx->buffer + nw, sctx->buf_used - nw);
        sctx->buf_used -= nw;

        if (sctx->buf_used == 0) {
            lsquic_stream_wantwrite(stream, 0);
            lsquic_stream_shutdown(stream, 1);   /* Echo complete */
        }
    } else if (nw < 0) {
        lsquic_stream_close(stream);
    }
}

static void
on_close(lsquic_stream_t *stream, lsquic_stream_ctx_t *h)
{
    struct stream_ctx *sctx = (struct stream_ctx *)h;
    if (sctx) {
        free(sctx->buffer);
        free(sctx);
    }
}

static const struct lsquic_stream_if stream_if = {
    .on_new_conn    = on_new_conn,
    .on_conn_closed = on_conn_closed,
    .on_new_stream  = on_new_stream,
    .on_read        = on_read,
    .on_write       = on_write,
    .on_close       = on_close,
};

/* ====================== Signal Handler ====================== */

static void sigint_handler(int sig)
{
    running = 0;
}

/* ====================== Main ====================== */

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <listen_ip> <port> <cert.pem> <key.pem>\n", argv[0]);
        return 1;
    }

    const char *listen_ip = argv[1];
    int port = atoi(argv[2]);
    const char *cert_file = argv[3];
    const char *key_file  = argv[4];

    signal(SIGINT, sigint_handler);

    /* 1. liburing setup */
    if (io_uring_queue_init(RING_QUEUE_DEPTH, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        return 1;
    }

    /* 2. Create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (sockfd < 0) {
        perror("socket");
        goto cleanup;
    }

    int reuse = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, listen_ip, &addr.sin_addr);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        goto cleanup;
    }

    printf("QUIC Echo Server listening on %s:%d (io_uring)\n", listen_ip, port);

    /* 3. lsquic setup */
    if (0 != lsquic_global_init(LSQUIC_GLOBAL_SERVER)) {
        fprintf(stderr, "lsquic_global_init failed\n");
        goto cleanup;
    }

    struct lsquic_engine_settings settings;
    lsquic_engine_init_settings(&settings, LSENG_SERVER);

    struct lsquic_engine_api api = {
        .ea_packets_out     = packets_out,
        .ea_stream_if       = &stream_if,
        .ea_settings        = &settings,
    };

    lsquic_engine_t *engine = lsquic_engine_new(LSENG_SERVER, &api);
    if (!engine) {
        fprintf(stderr, "lsquic_engine_new failed\n");
        goto cleanup;
    }

    /* 4. Submit initial multishot recvmsg (or multiple recv) */
    /* For simplicity we use regular recvmsg in a loop with io_uring */

    while (running) {
        struct io_uring_cqe *cqe;
        unsigned head, count = 0;

        /* Submit pending work */
        io_uring_submit(&ring);

        /* Wait for completion */
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0 && ret != -EINTR) {
            fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
            break;
        }

        io_uring_for_each_cqe(&ring, head, cqe) {
            count++;

            if (cqe->user_data == 0) {
                /* This is a send completion - ignore for now */
            } else {
                /* recv completion */
                struct msghdr *msg = (struct msghdr *)cqe->user_data;
                if (cqe->res > 0) {
                    lsquic_engine_packet_in(engine,
                        (const unsigned char *)msg->msg_iov->iov_base,
                        cqe->res,
                        (const struct sockaddr *)&addr,          /* local */
                        (const struct sockaddr *)msg->msg_name,  /* peer */
                        NULL, 0);
                }
                /* Re-arm recv */
                /* In a real implementation you would reuse buffers and resubmit */
            }
        }

        io_uring_cq_advance(&ring, count);

        /* Process lsquic timers and internal work */
        lsquic_engine_process_conns(engine);
    }

    printf("Shutting down...\n");

cleanup:
    if (engine) lsquic_engine_destroy(engine);
    lsquic_global_cleanup();
    if (sockfd >= 0) close(sockfd);
    io_uring_queue_exit(&ring);

    return 0;
}

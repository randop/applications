/* 
 * QUIC Echo Server with liburing (io_uring)
 *
 * Run:
 *   ./server 0.0.0.0 4433 cert.pem key.pem
 */

#include <lsquic.h>
#include <liburing.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <openssl/ssl.h>

#define RING_DEPTH 512
#define MAX_PKT_SZ 65536

/* ================= globals ================= */

static struct io_uring ring;
static int sockfd;
static volatile int running = 1;
static SSL_CTX *global_ctx = nullptr;

/* ================= contexts ================= */

struct conn_ctx {
    int dummy;
};

struct stream_ctx {
    char *buf = nullptr;
    size_t len = 0;
};

/* ================= helpers ================= */

static inline socklen_t sa_len(const sockaddr *sa)
{
    return sa->sa_family == AF_INET
        ? sizeof(sockaddr_in)
        : sizeof(sockaddr_in6);
}

/* ================= packet out ================= */

struct send_req {
    msghdr msg;
};

static int
packets_out(void *, const lsquic_out_spec *specs, unsigned n_specs)
{
    for (unsigned i = 0; i < n_specs; i++) {
        const lsquic_out_spec *spec = &specs[i];

        io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (!sqe) break;

        send_req *req = (send_req*)malloc(sizeof(send_req));
        memset(req, 0, sizeof(send_req));

        req->msg.msg_name    = (void*)spec->dest_sa;
        req->msg.msg_namelen = sa_len(spec->dest_sa);
        req->msg.msg_iov     = (iovec*)spec->iov;
        req->msg.msg_iovlen  = spec->iovlen;

        io_uring_prep_sendmsg(sqe, sockfd, &req->msg, 0);
        io_uring_sqe_set_data(sqe, req);
    }

    io_uring_submit(&ring);
    return n_specs;
}

/* ================= stream callbacks ================= */

static lsquic_conn_ctx_t *
on_new_conn(void *, lsquic_conn_t *conn)
{
    conn_ctx *c = (conn_ctx*)calloc(1, sizeof(conn_ctx));
    lsquic_conn_set_ctx(conn, (lsquic_conn_ctx_t*)c);
    return (lsquic_conn_ctx_t*)c;
}

static void
on_conn_closed(lsquic_conn_t *conn)
{
    conn_ctx *c = (conn_ctx*)lsquic_conn_get_ctx(conn);
    free(c);
}

static lsquic_stream_ctx_t *
on_new_stream(void *, lsquic_stream_t *stream)
{
    stream_ctx *s = (stream_ctx*)calloc(1, sizeof(stream_ctx));
    lsquic_stream_set_ctx(stream, (lsquic_stream_ctx_t*)s);
    lsquic_stream_wantread(stream, 1);
    return (lsquic_stream_ctx_t*)s;
}

static void
on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *h)
{
    stream_ctx *s = (stream_ctx*)h;

    char tmp[4096];
    ssize_t n;

    while ((n = lsquic_stream_read(stream, tmp, sizeof(tmp))) > 0) {
        s->buf = (char*)realloc(s->buf, s->len + n);
        memcpy(s->buf + s->len, tmp, n);
        s->len += n;
    }

    if (n == 0) {
        lsquic_stream_wantwrite(stream, 1);
        lsquic_stream_wantread(stream, 0);
    }
}

static void
on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *h)
{
    stream_ctx *s = (stream_ctx*)h;

    if (!s->len) {
        lsquic_stream_wantwrite(stream, 0);
        return;
    }

    ssize_t n = lsquic_stream_write(stream, s->buf, s->len);
    if (n > 0) {
        memmove(s->buf, s->buf + n, s->len - n);
        s->len -= n;
    }
}

static void
on_close(lsquic_stream_t *, lsquic_stream_ctx_t *h)
{
    stream_ctx *s = (stream_ctx*)h;
    free(s->buf);
    free(s);
}

/* ================= stream_if (FIXED RUNTIME INIT) ================= */

static lsquic_stream_if stream_if;

static void init_stream_if()
{
    memset(&stream_if, 0, sizeof(stream_if));

    stream_if.on_new_conn    = on_new_conn;
    stream_if.on_conn_closed = on_conn_closed;
    stream_if.on_new_stream  = on_new_stream;

    stream_if.on_read        = on_read;
    stream_if.on_write       = on_write;
    stream_if.on_close       = on_close;
}

/* ================= TLS (REQUIRED FOR YOUR BUILD) ================= */

static SSL_CTX *
get_ssl_ctx(void *, const sockaddr *)
{
    if (!global_ctx) {
        global_ctx = SSL_CTX_new(TLS_server_method());

        SSL_CTX_use_certificate_file(global_ctx, "cert.pem", SSL_FILETYPE_PEM);
        SSL_CTX_use_PrivateKey_file(global_ctx, "key.pem", SSL_FILETYPE_PEM);
    }

    return global_ctx;
}

/* ================= recv ================= */

struct recv_req {
    msghdr msg;
    iovec iov;
    char buf[MAX_PKT_SZ];
    sockaddr_storage peer;
};

static void submit_recv()
{
    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe) return;

    recv_req *r = (recv_req*)malloc(sizeof(recv_req));
    memset(r, 0, sizeof(recv_req));

    r->iov.iov_base = r->buf;
    r->iov.iov_len  = sizeof(r->buf);

    r->msg.msg_name    = &r->peer;
    r->msg.msg_namelen = sizeof(r->peer);
    r->msg.msg_iov     = &r->iov;
    r->msg.msg_iovlen  = 1;

    io_uring_prep_recvmsg(sqe, sockfd, &r->msg, 0);
    io_uring_sqe_set_data(sqe, r);
}

/* ================= signal ================= */

static void on_sig(int)
{
    running = 0;
}

/* ================= main ================= */

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s ip port\n", argv[0]);
        return 1;
    }

    signal(SIGINT, on_sig);

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    io_uring_queue_init(RING_DEPTH, &ring, 0);

    sockfd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    bind(sockfd, (sockaddr*)&addr, sizeof(addr));

    lsquic_global_init(LSQUIC_GLOBAL_SERVER);

    lsquic_engine_settings settings;
    lsquic_engine_init_settings(&settings, LSENG_SERVER);

    init_stream_if();

    lsquic_engine_api api{};
    api.ea_packets_out     = packets_out;
    api.ea_stream_if       = &stream_if;
    api.ea_settings        = &settings;

    /* 🔥 REQUIRED TLS HOOK (THIS FIXES YOUR BUILD) */
    api.ea_get_ssl_ctx = get_ssl_ctx;

    lsquic_engine_t *engine =
        lsquic_engine_new(LSENG_SERVER, &api);

    for (int i = 0; i < 64; i++)
        submit_recv();

    while (running) {
        io_uring_submit(&ring);

        io_uring_cqe *cqe;
        if (io_uring_wait_cqe(&ring, &cqe) < 0)
            continue;

        do {
            void *data = io_uring_cqe_get_data(cqe);

            if (data) {
                recv_req *r = (recv_req*)data;

                if (cqe->res > 0) {
                    lsquic_engine_packet_in(
                        engine,
                        (unsigned char*)r->buf,
                        cqe->res,
                        (sockaddr*)&addr,
                        (sockaddr*)&r->peer,
                        NULL,
                        0
                    );
                }

                free(r);
                submit_recv();
            }

            io_uring_cqe_seen(&ring, cqe);

        } while (io_uring_peek_cqe(&ring, &cqe) == 0);

        lsquic_engine_process_conns(engine);
    }

    lsquic_engine_destroy(engine);
    lsquic_global_cleanup();

    io_uring_queue_exit(&ring);
    close(sockfd);

    return 0;
}

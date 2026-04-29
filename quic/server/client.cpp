/*
 * QUIC Echo client with liburing (io_uring)
 *
 * Run:
 *   ./client.bin 0.0.0.0 4433
 */

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <liburing.h>
#include <lsquic.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#define RING_DEPTH 256
#define MAX_PKT_SZ 65536

static struct io_uring ring;
static int sockfd;
static volatile int running = 1;

static SSL_CTX *global_ctx = nullptr;

static SSL_CTX *get_ssl_ctx(void *, const sockaddr *) {
  if (!global_ctx) {
    global_ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_verify(global_ctx, SSL_VERIFY_NONE, nullptr);
  }
  return global_ctx;
}

struct stream_ctx {
  char *buf = nullptr;
  size_t len = 0;
};

static lsquic_conn_ctx_t *on_new_conn(void *, lsquic_conn_t *) {
  return nullptr;
}

static void on_conn_closed(lsquic_conn_t *) {}

static lsquic_stream_ctx_t *on_new_stream(void *, lsquic_stream_t *stream) {
  stream_ctx *s = (stream_ctx *)calloc(1, sizeof(stream_ctx));
  lsquic_stream_set_ctx(stream, (lsquic_stream_ctx_t *)s);

  /* IMPORTANT: defer write to allow stream readiness */
  lsquic_stream_wantwrite(stream, 1);
  lsquic_stream_wantread(stream, 1);

  return (lsquic_stream_ctx_t *)s;
}

static void on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *h) {
  stream_ctx *s = (stream_ctx *)h;

  char tmp[4096];
  ssize_t n;

  while ((n = lsquic_stream_read(stream, tmp, sizeof(tmp))) > 0) {
    s->buf = (char *)realloc(s->buf, s->len + n);
    memcpy(s->buf + s->len, tmp, n);
    s->len += n;
  }

  if (n == 0 && s->len) {
    fwrite(s->buf, 1, s->len, stdout);
    fflush(stdout);
  }
}

static void on_write(lsquic_stream_t *, lsquic_stream_ctx_t *) {
  static const char msg[] = "hello\n";

  lsquic_stream_write(stream, msg, sizeof(msg) - 1);
  lsquic_stream_shutdown(stream, 1);

  lsquic_stream_wantwrite(stream, 0);
}

static void on_close(lsquic_stream_t *, lsquic_stream_ctx_t *h) {
  stream_ctx *s = (stream_ctx *)h;
  free(s->buf);
  free(s);
}

static lsquic_stream_if stream_if;

static void init_stream_if() {
  memset(&stream_if, 0, sizeof(stream_if));

  stream_if.on_new_conn = on_new_conn;
  stream_if.on_conn_closed = on_conn_closed;
  stream_if.on_new_stream = on_new_stream;
  stream_if.on_read = on_read;
  stream_if.on_write = on_write;
  stream_if.on_close = on_close;
}

struct recv_req {
  msghdr msg;
  iovec iov;
  char buf[MAX_PKT_SZ];
  sockaddr_storage peer;
};

static void submit_recv() {
  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  if (!sqe) {
    return;
  }

  recv_req *r = (recv_req *)malloc(sizeof(recv_req));
  memset(r, 0, sizeof(recv_req));

  r->iov.iov_base = r->buf;
  r->iov.iov_len = sizeof(r->buf);

  r->msg.msg_name = &r->peer;
  r->msg.msg_namelen = sizeof(r->peer);
  r->msg.msg_iov = &r->iov;
  r->msg.msg_iovlen = 1;

  io_uring_prep_recvmsg(sqe, sockfd, &r->msg, 0);
  io_uring_sqe_set_data(sqe, r);
}

struct send_req {
  msghdr msg;
};

static int packets_out(void *, const lsquic_out_spec *specs, unsigned n_specs) {
  for (unsigned i = 0; i < n_specs; i++) {
    const lsquic_out_spec *spec = &specs[i];

    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
      break;
    }

    send_req *r = (send_req *)malloc(sizeof(send_req));
    memset(r, 0, sizeof(send_req));

    r->msg.msg_name = (void *)spec->dest_sa;
    r->msg.msg_namelen = sizeof(sockaddr_in);
    r->msg.msg_iov = (iovec *)spec->iov;
    r->msg.msg_iovlen = spec->iovlen;

    io_uring_prep_sendmsg(sqe, sockfd, &r->msg, 0);
    io_uring_sqe_set_data(sqe, r);
  }

  io_uring_submit(&ring);
  return n_specs;
}

static void on_sig(int) { running = 0; }

int main(int argc, char **argv) {
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

  bind(sockfd, (sockaddr *)&addr, sizeof(addr));

  lsquic_global_init(LSQUIC_GLOBAL_CLIENT);

  lsquic_engine_settings settings;
  lsquic_engine_init_settings(&settings, LSENG_SERVER);

  init_stream_if();

  lsquic_engine_api api{};
  api.ea_packets_out = packets_out;
  api.ea_stream_if = &stream_if;
  api.ea_settings = &settings;

  api.ea_get_ssl_ctx = get_ssl_ctx;

  lsquic_engine_t *engine = lsquic_engine_new(LSENG_SERVER, &api);

  lsquic_conn_t *conn =
      lsquic_engine_connect(engine, LSQVER_I001, nullptr, /* local address */
                            (sockaddr *)&addr,            /* peer address */
                            nullptr,                      /* peer ctx */
                            nullptr,                      /* conn ctx */
                            nullptr,                      /* hostname */
                            0,                            /* sni length */
                            nullptr, 0,                   /* alpn */
                            nullptr, 0 /* session resumption */
      );

  for (int i = 0; i < 64; i++) {
    submit_recv();
  }

  while (running) {
    io_uring_submit(&ring);

    io_uring_cqe *cqe;
    if (io_uring_wait_cqe(&ring, &cqe) < 0) {
      continue;
    }

    do {
      void *data = io_uring_cqe_get_data(cqe);

      if (data) {
        recv_req *r = (recv_req *)data;

        if (cqe->res > 0) {
          lsquic_engine_packet_in(engine, (unsigned char *)r->buf, cqe->res,
                                  (sockaddr *)&addr, (sockaddr *)&r->peer,
                                  nullptr, 0);
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

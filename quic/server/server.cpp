/*
 * QUIC Echo Server with liburing (io_uring)
 *
 * Run:
 *   ./server.bin 0.0.0.0 4433
 */
// server.c
// QUIC + io_uring buffer-ring integration (C)

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/udp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "liburing.h"
#include <lsquic.h>

#define QD 64
#define BUF_SHIFT 12
#define CQES (QD * 16)
#define BUFFERS CQES
#define CONTROLLEN 0

/* ================= LSQUIC CONTEXT ================= */

static struct lsquic_engine *engine;
static struct io_uring ring;
static int sockfd;

struct sendmsg_ctx {
  struct msghdr msg;
  struct iovec iov;
};

struct ctx {
  struct io_uring ring;
  struct io_uring_buf_ring *buf_ring;
  unsigned char *buffer_base;
  struct msghdr msg;
  int buf_shift;
  int af;
  bool verbose;
  struct sendmsg_ctx send[BUFFERS];
  size_t buf_ring_size;
};

static struct ctx gctx;

/* ================= BUFFER HELPERS ================= */

static size_t buffer_size(struct ctx *ctx) { return 1U << ctx->buf_shift; }

static unsigned char *get_buffer(struct ctx *ctx, int idx) {
  return ctx->buffer_base + (idx << ctx->buf_shift);
}

/* ================= LSQUIC CALLBACKS ================= */

struct stream_ctx {
  char dummy;
};

static lsquic_stream_ctx_t *on_new_stream(void *ctx, lsquic_stream_t *s) {
  struct stream_ctx *sc = calloc(1, sizeof(*sc));

  printf("[lsquic] new stream\n");

  lsquic_stream_set_ctx(s, sc);
  lsquic_stream_wantread(s, 1);

  return (lsquic_stream_ctx_t *)sc;
}

static void on_read(lsquic_stream_t *s, lsquic_stream_ctx_t *h) {
  char buf[1024];
  ssize_t n;

  while ((n = lsquic_stream_read(s, buf, sizeof(buf))) > 0) {
    printf("[lsquic] read %ld bytes\n", n);
  }

  if (n == 0) {
    lsquic_stream_wantwrite(s, 1);
  }
}

static void on_write(lsquic_stream_t *s, lsquic_stream_ctx_t *h) {
  const char *msg = "echo-from-io_uring-lsquic";

  lsquic_stream_write(s, msg, strlen(msg));
  printf("[lsquic] echo sent\n");

  lsquic_stream_shutdown(s, 1);
}

static void on_close(lsquic_stream_t *s, lsquic_stream_ctx_t *h) { free(h); }

static lsquic_conn_ctx_t *on_conn_new(void *ctx, lsquic_conn_t *c) {
  printf("[lsquic] conn created\n");
  return NULL;
}

static void on_conn_close(lsquic_conn_t *c) {
  printf("[lsquic] conn closed\n");
}

static const struct lsquic_stream_if stream_if = {
    .on_new_conn = on_conn_new,
    .on_conn_closed = on_conn_close,
    .on_new_stream = on_new_stream,
    .on_read = on_read,
    .on_write = on_write,
    .on_close = on_close};

/* ================= PACKETS OUT ================= */

static int packets_out(void *ctx, const struct lsquic_out_spec *specs,
                       unsigned n_specs) {
  for (unsigned i = 0; i < n_specs; i++) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&gctx.ring);
    if (!sqe) {
      continue;
    }

    struct msghdr *msg = malloc(sizeof(*msg));
    memset(msg, 0, sizeof(*msg));

    msg->msg_name = (void *)specs[i].dest_sa;
    msg->msg_namelen = specs[i].dest_sa_len;
    msg->msg_iov = (struct iovec *)specs[i].iov;
    msg->msg_iovlen = specs[i].iovlen;

    io_uring_prep_sendmsg(sqe, sockfd, msg, 0);
    io_uring_sqe_set_data(sqe, msg);
  }

  printf("[io_uring] packets_out=%u\n", n_specs);
  return n_specs;
}

/* ================= RECV SUBMISSION ================= */

static void submit_recv(int idx) {
  struct io_uring_sqe *sqe = io_uring_get_sqe(&gctx.ring);
  if (!sqe) {
    return;
  }

  struct msghdr *msg = malloc(sizeof(*msg));
  memset(msg, 0, sizeof(*msg));

  static char buf[2048];

  struct iovec *iov = malloc(sizeof(*iov));
  iov->iov_base = buf;
  iov->iov_len = sizeof(buf);

  msg->msg_name = malloc(sizeof(struct sockaddr_storage));
  msg->msg_namelen = sizeof(struct sockaddr_storage);
  msg->msg_iov = iov;
  msg->msg_iovlen = 1;

  io_uring_prep_recvmsg(sqe, sockfd, msg, 0);
  io_uring_sqe_set_data(sqe, msg);
}

/* ================= MAIN LOOP ================= */

int main() {
  io_uring_queue_init(QD, &gctx.ring, 0);

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);

  struct sockaddr_in addr = {.sin_family = AF_INET,
                             .sin_port = htons(4433),
                             .sin_addr.s_addr = INADDR_ANY};

  bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));

  printf("[server] io_uring + lsquic starting\n");

  lsquic_global_init(LSQUIC_GLOBAL_SERVER);

  struct lsquic_engine_settings settings;
  lsquic_engine_init_settings(&settings, LSENG_SERVER);

  struct lsquic_engine_api api;
  memset(&api, 0, sizeof(api));

  api.ea_stream_if = &stream_if;
  api.ea_packets_out = packets_out;

  engine = lsquic_engine_new(LSENG_SERVER, &api);

  submit_recv(0);

  io_uring_submit(&gctx.ring);

  while (1) {
    struct io_uring_cqe *cqe;

    io_uring_wait_cqe(&gctx.ring, &cqe);

    struct msghdr *msg = io_uring_cqe_get_data(cqe);

    if (cqe->res > 0) {
      printf("[io_uring] rx %d bytes\n", cqe->res);

      lsquic_engine_packet_in(engine, (unsigned char *)msg->msg_iov->iov_base,
                              cqe->res, (struct sockaddr *)&addr,
                              (struct sockaddr *)msg->msg_name, NULL, 0);

      lsquic_engine_process_conns(engine);
    }

    io_uring_cqe_seen(&gctx.ring, cqe);

    submit_recv(0);
    io_uring_submit(&gctx.ring);

    lsquic_engine_process_conns(engine);
  }
}

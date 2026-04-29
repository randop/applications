/*
 * QUIC Echo Server with liburing (io_uring)
 *
 * Run:
 *   ./server.bin 0.0.0.0 4433
 */

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <liburing.h>
#include <unistd.h>

#define QD 1024
#define BUF_SZ 2048
#define RECVS 256

static io_uring ring;
static int sockfd;

struct recv_op {
  msghdr msg;
  iovec iov;
  sockaddr_storage peer;
  char data[BUF_SZ];
};

static recv_op ops[RECVS];

static void submit_recv(int i) {
  recv_op &op = ops[i];

  memset(&op.msg, 0, sizeof(op.msg));

  op.iov.iov_base = op.data;
  op.iov.iov_len = sizeof(op.data);

  op.msg.msg_name = &op.peer;
  op.msg.msg_namelen = sizeof(op.peer);
  op.msg.msg_iov = &op.iov;
  op.msg.msg_iovlen = 1;

  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  if (!sqe) {
    std::cout << "[reactor] SQE NULL (drop)" << std::endl;
    return;
  }

  io_uring_prep_recvmsg(sqe, sockfd, &op.msg, 0);
  io_uring_sqe_set_data(sqe, &op);

  std::cout << "[reactor] submit recv slot=" << i << std::endl;
}

static void refill_all() {
  for (int i = 0; i < RECVS; i++) {
    submit_recv(i);
  }

  io_uring_submit(&ring);
}

int main() {
  io_uring_queue_init(QD, &ring, 0);

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(4433);
  inet_pton(AF_INET, "0.0.0.0", &addr.sin_addr);

  bind(sockfd, (sockaddr *)&addr, sizeof(addr));

  std::cout << "[reactor] STARTED" << std::endl;

  refill_all();

  while (true) {
    io_uring_cqe *cqe;

    int ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
      std::cout << "[reactor] wait error " << ret << std::endl;
      continue;
    }

    recv_op *op = (recv_op *)io_uring_cqe_get_data(cqe);

    if (cqe->res > 0) {
      std::cout << "[reactor] RX bytes=" << cqe->res << std::endl;

      // 🔥 HERE is where LSQUIC would receive packet_in
      // lsquic_engine_packet_in(...)

    } else {
      std::cout << "[reactor] RX error=" << cqe->res << std::endl;
    }

    io_uring_cqe_seen(&ring, cqe);

    // CRITICAL: immediately re-arm same slot
    submit_recv(op - ops);
    io_uring_submit(&ring);
  }
}

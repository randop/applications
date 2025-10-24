#include <arpa/inet.h> // For general socket utils (future-proof)
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h> // For fcntl
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <queue> // For pending_fds
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" {
#include <lsquic.h>
#include <lsquic_types.h>
}

#define MAX_POLL_FDS 1024
#define IDLE_TIMEOUT_US 10000000  // 10 seconds for idle conn
#define PROXY_IDLE_TIMEOUT_SEC 10 // Robust timeout for proxy pairs

// Custom ALPN for tunnel
static const char *alpn = "h3";

// Forward declaration for send_packets_out (used before definition)
static int send_packets_out(void *ctx, const lsquic_out_spec *specs,
                            unsigned n_specs);

// ProxyPair structure for tunneling
struct ProxyPair {
  lsquic_stream_t *stream;
  int tcp_fd;
  std::vector<char> to_stream_buf; // Buffer from TCP to stream
  std::vector<char> to_tcp_buf;    // Buffer from stream to TCP
  std::chrono::steady_clock::time_point last_activity;
  bool closed = false;
};

// Global variables
lsquic_engine_t *engine;
int quic_udp_fd;
int tcp_listen_fd;
lsquic_conn_t *tunnel_conn = nullptr;
std::map<int, ProxyPair *> fd_to_proxy; // TCP fd to ProxyPair
std::map<lsquic_stream_t *, ProxyPair *> stream_to_proxy;
std::queue<int>
    pending_fds; // Queue for pending TCP fds (for server-initiated streams)
SSL_CTX *ssl_ctx = nullptr;

// SSL callback
static SSL_CTX *get_ssl_ctx(void *peer_ctx, const struct sockaddr *local_sa) {
  (void)peer_ctx;
  (void)local_sa;
  return ssl_ctx;
}

// Callbacks
static lsquic_conn_ctx_t *server_on_new_conn(void *stream_if_ctx,
                                             lsquic_conn_t *conn) {
  (void)stream_if_ctx;
  tunnel_conn = conn;
  std::cout << "Tunnel connection established" << std::endl;
  return (lsquic_conn_ctx_t *)conn;
}

static void server_on_conn_closed(lsquic_conn_t *conn) {
  if (conn == tunnel_conn) {
    tunnel_conn = nullptr;
    std::cout << "Tunnel connection closed" << std::endl;
    for (auto &p : fd_to_proxy) {
      close(p.first);
    }
    fd_to_proxy.clear();
    stream_to_proxy.clear();
    while (!pending_fds.empty()) {
      close(pending_fds.front());
      pending_fds.pop();
    }
  }
}

static lsquic_stream_ctx_t *server_on_new_stream(void *stream_if_ctx,
                                                 lsquic_stream_t *stream) {
  (void)stream_if_ctx;
  if (pending_fds.empty()) {
    return nullptr;
  }
  int fd = pending_fds.front();
  pending_fds.pop();
  ProxyPair *pair =
      new ProxyPair{stream, fd, {}, {}, std::chrono::steady_clock::now()};
  fd_to_proxy[fd] = pair;
  stream_to_proxy[stream] = pair;
  lsquic_stream_wantread(stream, 1);
  lsquic_stream_wantwrite(stream, 0);
  return (lsquic_stream_ctx_t *)pair;
}

static void server_on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
  (void)ctx;
  ProxyPair *pair = stream_to_proxy[stream];
  if (!pair || pair->closed)
    return;

  char buf[4096];
  ssize_t n = lsquic_stream_read(stream, buf, sizeof(buf));
  if (n > 0) {
    pair->last_activity = std::chrono::steady_clock::now();
    ssize_t written = write(pair->tcp_fd, buf, n);
    if (written < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        written = 0;
      } else {
        pair->closed = true;
        lsquic_stream_close(stream);
        return;
      }
    }
    if (written < n) {
      pair->to_tcp_buf.insert(pair->to_tcp_buf.end(), buf + written, buf + n);
    }
    lsquic_stream_wantread(stream, (n == sizeof(buf)) ? 1 : 0);
  } else if (n == 0) {
    pair->closed = true;
    lsquic_stream_shutdown(stream, 0);
  } else if (n < 0) {
    pair->closed = true;
    lsquic_stream_close(stream);
  }
}

static void server_on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
  (void)ctx;
  ProxyPair *pair = stream_to_proxy[stream];
  if (!pair || pair->closed)
    return;

  if (!pair->to_stream_buf.empty()) {
    ssize_t written = lsquic_stream_write(stream, pair->to_stream_buf.data(),
                                          pair->to_stream_buf.size());
    if (written > 0) {
      pair->last_activity = std::chrono::steady_clock::now();
      pair->to_stream_buf.erase(pair->to_stream_buf.begin(),
                                pair->to_stream_buf.begin() + written);
    }
    if (written <= 0) {
      pair->closed = true;
      lsquic_stream_close(stream);
    }
  }
  lsquic_stream_wantwrite(stream, pair->to_stream_buf.empty() ? 0 : 1);
}

static void server_on_close(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
  (void)ctx;
  ProxyPair *pair = stream_to_proxy[stream];
  if (pair) {
    if (!pair->closed) {
      close(pair->tcp_fd);
    }
    fd_to_proxy.erase(pair->tcp_fd);
    stream_to_proxy.erase(stream);
    delete pair;
  }
}

static const lsquic_stream_if stream_callbacks = {
    .on_new_conn = server_on_new_conn,
    .on_goaway_received = NULL,
    .on_conn_closed = server_on_conn_closed,
    .on_new_stream = server_on_new_stream,
    .on_read = server_on_read,
    .on_write = server_on_write,
    .on_close = server_on_close,
    .on_dg_write = NULL,
    .on_datagram = NULL,
    .on_hsk_done = NULL,
    .on_new_token = NULL,
    .on_sess_resume_info = NULL,
    .on_reset = NULL,
    .on_conncloseframe_received = NULL,
};

static int send_packets_out(void *ctx, const lsquic_out_spec *specs,
                            unsigned n_specs) {
  int sockfd = (int)(uintptr_t)ctx;
  unsigned n_sent = 0;
  for (unsigned i = 0; i < n_specs; ++i) {
    struct msghdr msg = {};
    msg.msg_name = (void *)specs[i].dest_sa;
    msg.msg_namelen = specs[i].dest_sa->sa_family == AF_INET
                          ? sizeof(struct sockaddr_in)
                          : sizeof(struct sockaddr_in6);
    msg.msg_iov = specs[i].iov;
    msg.msg_iovlen = specs[i].iovlen;
    if (sendmsg(sockfd, &msg, 0) < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return n_sent;
      return -1;
    }
    ++n_sent;
  }
  return n_sent;
}

// Create sockets
int create_udp_socket(uint16_t port) {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0)
    return -1;
  struct sockaddr_in sa = {};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = INADDR_ANY;
  if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    close(s);
    return -1;
  }
  return s;
}

int create_tcp_listener(uint16_t port) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0)
    return -1;
  int opt = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  struct sockaddr_in sa = {};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = INADDR_ANY;
  if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0 || listen(s, 128) < 0) {
    close(s);
    return -1;
  }
  return s;
}

// Main
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  if (lsquic_global_init(LSQUIC_GLOBAL_SERVER) != 0) {
    std::cerr << "LSQUIC init failed" << std::endl;
    return EXIT_FAILURE;
  }

  // Initialize OpenSSL
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();

  ssl_ctx = SSL_CTX_new(TLS_method());
  if (!ssl_ctx) {
    std::cerr << "SSL_CTX_new failed" << std::endl;
    ERR_print_errors_fp(stderr);
    return EXIT_FAILURE;
  }

  // Load certificate and private key - replace with actual paths
  if (SSL_CTX_use_certificate_file(
          ssl_ctx,
          "/home/randolph/projects/applications/quic-rproxy/certs/cert.pem",
          SSL_FILETYPE_PEM) <= 0) {
    std::cerr << "Failed to load certificate" << std::endl;
    ERR_print_errors_fp(stderr);
    SSL_CTX_free(ssl_ctx);
    return EXIT_FAILURE;
  }
  if (SSL_CTX_use_PrivateKey_file(
          ssl_ctx,
          "/home/randolph/projects/applications/quic-rproxy/certs/key.pem",
          SSL_FILETYPE_PEM) <= 0) {
    std::cerr << "Failed to load private key" << std::endl;
    ERR_print_errors_fp(stderr);
    SSL_CTX_free(ssl_ctx);
    return EXIT_FAILURE;
  }

  lsquic_engine_settings es = {};
  lsquic_engine_init_settings(&es, LSENG_SERVER);
  es.es_idle_conn_to = IDLE_TIMEOUT_US;

  lsquic_engine_api engine_api = {};
  engine_api.ea_settings = &es;
  engine_api.ea_stream_if = &stream_callbacks;
  engine_api.ea_packets_out = send_packets_out;
  engine_api.ea_alpn = alpn;
  engine_api.ea_packets_out_ctx = nullptr;
  engine_api.ea_get_ssl_ctx = get_ssl_ctx;

  quic_udp_fd = create_udp_socket(4433);
  if (quic_udp_fd < 0) {
    std::cerr << "LSQUIC create_udp_socket failed" << std::endl;
    return EXIT_FAILURE;
  }
  engine_api.ea_packets_out_ctx = (void *)(uintptr_t)quic_udp_fd;

  engine = lsquic_engine_new(LSENG_SERVER, &engine_api);
  if (!engine) {
    std::cerr << "LSQUIC engine new failed" << std::endl;
    return EXIT_FAILURE;
  }

  tcp_listen_fd = create_tcp_listener(8080);
  if (tcp_listen_fd < 0) {
    std::cerr << "create_tcp_listener failed" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "Listening on TCP port 8080..." << std::endl;

  std::vector<struct pollfd> pollfds;
  pollfds.reserve(MAX_POLL_FDS);
  pollfds.push_back({quic_udp_fd, POLLIN, 0});
  pollfds.push_back({tcp_listen_fd, POLLIN, 0});

  while (true) {
    int timeout_ms = -1;
    int diff;
    if (lsquic_engine_earliest_adv_tick(engine, &diff)) {
      timeout_ms = (diff > 0) ? diff / 1000 : 0;
    }

    // Add pollfds for active TCP fds
    pollfds.resize(2);
    size_t idx = 2;
    for (auto &p : fd_to_proxy) {
      short events = p.second->closed ? 0 : POLLIN;
      if (!p.second->to_tcp_buf.empty())
        events |= POLLOUT;
      if (events != 0) {
        if (idx >= MAX_POLL_FDS)
          break;
        pollfds[idx++] = {p.first, events, 0};
      }
    }
    pollfds.resize(idx);

    int ret = poll(pollfds.data(), pollfds.size(), timeout_ms);
    (void)ret; // Can check for errors if needed

    // Handle QUIC UDP input
    if (pollfds[0].revents & POLLIN) {
      char buf[0x1000];
      struct sockaddr_storage peer_ss;
      socklen_t peer_len = sizeof(peer_ss);
      ssize_t n = recvfrom(quic_udp_fd, buf, sizeof(buf), 0,
                           (struct sockaddr *)&peer_ss, &peer_len);
      if (n > 0) {
        struct sockaddr_storage local_ss;
        socklen_t local_len = sizeof(local_ss);
        if (getsockname(quic_udp_fd, (struct sockaddr *)&local_ss,
                        &local_len) == 0) {
          lsquic_engine_packet_in(
              engine, (unsigned char *)buf, n, (struct sockaddr *)&local_ss,
              (struct sockaddr *)&peer_ss, (void *)(uintptr_t)quic_udp_fd, 0);
        } else {
          std::cerr << "getsockname failed" << std::endl;
        }
      }
    }

    // Handle TCP accept
    if (pollfds[1].revents & POLLIN) {
      struct sockaddr_in client_sa;
      socklen_t len = sizeof(client_sa);
      int new_fd = accept(tcp_listen_fd, (struct sockaddr *)&client_sa, &len);
      if (new_fd >= 0) {
        // Set non-blocking
        int flags = fcntl(new_fd, F_GETFL, 0);
        fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
        if (tunnel_conn) {
          pending_fds.push(new_fd);
          lsquic_conn_make_stream(tunnel_conn);
        } else {
          close(new_fd);
        }
      }
    }

    // Handle proxy TCP fds
    idx = 2;
    auto now = std::chrono::steady_clock::now();
    for (auto it = fd_to_proxy.begin(); it != fd_to_proxy.end();) {
      if (idx >= pollfds.size())
        break;
      struct pollfd &pfd = pollfds[idx++];
      ProxyPair *pair = it->second;
      if (pair->closed && pair->to_tcp_buf.empty()) {
        // Clean up
        close(pair->tcp_fd);
        stream_to_proxy.erase(pair->stream);
        delete pair;
        auto to_erase = it;
        ++it;
        fd_to_proxy.erase(to_erase);
        continue;
      }
      if (!pair->closed && (pfd.revents & POLLIN)) {
        char buf[4096];
        ssize_t n = read(pfd.fd, buf, sizeof(buf));
        if (n > 0) {
          pair->last_activity = now;
          pair->to_stream_buf.insert(pair->to_stream_buf.end(), buf, buf + n);
          lsquic_stream_wantwrite(pair->stream, 1);
        } else if (n == 0) {
          pair->closed = true;
          lsquic_stream_shutdown(pair->stream, 1);
        } else { // n < 0
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            pair->closed = true;
            lsquic_stream_close(pair->stream);
          }
        }
      }
      if ((pfd.revents & POLLOUT) && !pair->to_tcp_buf.empty()) {
        ssize_t written =
            write(pfd.fd, pair->to_tcp_buf.data(), pair->to_tcp_buf.size());
        if (written > 0) {
          pair->last_activity = now;
          pair->to_tcp_buf.erase(pair->to_tcp_buf.begin(),
                                 pair->to_tcp_buf.begin() + written);
        } else if (written < 0) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            pair->closed = true;
            lsquic_stream_close(pair->stream);
          }
        }
      }
      // Timeout check
      if (!pair->closed && std::chrono::duration_cast<std::chrono::seconds>(
                               now - pair->last_activity)
                                   .count() > PROXY_IDLE_TIMEOUT_SEC) {
        pair->closed = true;
        lsquic_stream_close(pair->stream);
      }
      ++it;
    }

    lsquic_engine_process_conns(engine);
  }

  if (ssl_ctx) {
    SSL_CTX_free(ssl_ctx);
  }
  EVP_cleanup();
  lsquic_engine_destroy(engine);
  lsquic_global_cleanup();
  return EXIT_SUCCESS;
}

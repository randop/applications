#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h> // For fcntl
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>
#include <cstdio> // For fwrite, fflush
#include <string> // For std::string if not already included

extern "C" {
#include <lsquic.h>
#include <lsquic_logger.h>
#include <lsquic_types.h>
}

#include <openssl/err.h>
#include <openssl/ssl.h>

#define MAX_POLL_FDS 1024
#define IDLE_TIMEOUT_US 10000000 // 10 seconds
#define PROXY_IDLE_TIMEOUT_SEC 10
#define LOCAL_PORT 3000 // Tunnel to local service on this port

static const char *alpn = "h3";

// ProxyPair for client
struct ProxyPair {
  lsquic_stream_t *stream;
  int local_fd;
  std::vector<char> to_stream_buf;
  std::vector<char> to_local_buf;
  std::chrono::steady_clock::time_point last_activity;
  bool closed = false;
};

// Globals
lsquic_engine_t *engine;
int quic_udp_fd;
lsquic_conn_t *tunnel_conn = nullptr;
std::map<int, ProxyPair *> fd_to_proxy;
std::map<lsquic_stream_t *, ProxyPair *> stream_to_proxy;
SSL_CTX *client_ssl_ctx = nullptr;

// Logger callback for LSQUIC debugging
static int lsq_log_callback(void *ctx, const char *buf, size_t len) {
  fwrite(buf, 1, len, stdout);
  fflush(stdout);
  return 0;
}

static const struct lsquic_logger_if logger_if = {
    .log_buf = lsq_log_callback,
};

// SSL verify callback to debug verification issues
static int verify_callback(int preverify_ok, X509_STORE_CTX *x509_ctx) {
  if (!preverify_ok) {
    int err = X509_STORE_CTX_get_error(x509_ctx);
    std::cout << "SSL verify error: " << err << " - " << X509_verify_cert_error_string(err) << std::endl;
  }
  return 1; // Accept anyway for debugging; remove this in production to enforce verification
}

// SSL callback for client
static SSL_CTX *get_client_ssl_ctx(void *peer_ctx,
                                   const struct sockaddr *local_sa) {
  (void)peer_ctx;
  (void)local_sa;
  return client_ssl_ctx;
}

// Packets out callback
static int send_packets_out(void *ctx, const struct lsquic_out_spec *specs,
                            unsigned n_specs) {
  struct msghdr msg;
  int sockfd = (int)(uintptr_t)ctx;
  unsigned n;
  memset(&msg, 0, sizeof(msg));
  for (n = 0; n < n_specs; ++n) {
    msg.msg_name = (void *)specs[n].dest_sa;
    msg.msg_namelen = specs[n].dest_sa->sa_family == AF_INET
                          ? sizeof(struct sockaddr_in)
                          : sizeof(struct sockaddr_in6);
    msg.msg_iov = specs[n].iov;
    msg.msg_iovlen = specs[n].iovlen;
    if (sendmsg(sockfd, &msg, 0) < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return (int)n;
      }
      return -1;
    }
  }
  return (int)n;
}

// Additional callbacks for debugging
static void client_on_hsk_done(lsquic_conn_t *conn, enum lsquic_hsk_status status) {
  if (status == LSQ_HSK_OK || status == LSQ_HSK_RESUMED_OK) {
    std::cout << "Handshake successful" << std::endl;
  } else {
    std::cout << "Handshake failed with status " << status << std::endl;
  }
}

static void client_on_conncloseframe_received(lsquic_conn_t *conn, int app_error, uint64_t error_code, const char *reason, int reason_len) {
  std::cout << "Connection close frame received: app_error=" << app_error << " error_code=" << error_code
            << " reason=" << std::string(reason, reason_len) << std::endl;
}

static void client_on_reset(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx, int how) {
  std::cout << "Stream reset with how=" << how << std::endl;
}

// Callbacks for client
static lsquic_conn_ctx_t *client_on_new_conn(void *stream_if_ctx,
                                             lsquic_conn_t *conn) {
  (void)stream_if_ctx;
  tunnel_conn = conn;
  std::cout << "Client tunnel connected" << std::endl;
  return (lsquic_conn_ctx_t *)conn;
}

static void client_on_conn_closed(lsquic_conn_t *conn) {
  (void)conn;
  tunnel_conn = nullptr;
  std::cout << "Client tunnel closed" << std::endl;
  for (auto &p : fd_to_proxy) {
    close(p.first);
  }
  fd_to_proxy.clear();
  stream_to_proxy.clear();
}

static lsquic_stream_ctx_t *client_on_new_stream(void *stream_if_ctx,
                                                 lsquic_stream_t *stream) {
  (void)stream_if_ctx;
  // New stream from server: connect to local service
  int local_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (local_fd < 0)
    return nullptr;

  struct sockaddr_in local_sa = {};
  local_sa.sin_family = AF_INET;
  local_sa.sin_port = htons(LOCAL_PORT);
  local_sa.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (connect(local_fd, (struct sockaddr *)&local_sa, sizeof(local_sa)) < 0) {
    std::cout << "client_on_new_stream connect failed! closing..." << std::endl;
    close(local_fd);
    return nullptr;
  }

  // Set non-blocking
  int flags = fcntl(local_fd, F_GETFL, 0);
  fcntl(local_fd, F_SETFL, flags | O_NONBLOCK);

  ProxyPair *pair =
      new ProxyPair{stream, local_fd, {}, {}, std::chrono::steady_clock::now()};
  fd_to_proxy[local_fd] = pair;
  stream_to_proxy[stream] = pair;
  lsquic_stream_wantread(stream, 1);
  return (lsquic_stream_ctx_t *)pair;
}

static void client_on_read(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
  ProxyPair *pair = (ProxyPair *)ctx;
  if (pair->closed)
    return;

  char buf[4096];
  ssize_t n = lsquic_stream_read(stream, buf, sizeof(buf));
  if (n > 0) {
    pair->last_activity = std::chrono::steady_clock::now();
    ssize_t written = write(pair->local_fd, buf, n);
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
      pair->to_local_buf.insert(pair->to_local_buf.end(), buf + written,
                                buf + n);
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

static void client_on_write(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
  ProxyPair *pair = (ProxyPair *)ctx;
  if (pair->closed)
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

static void client_on_close(lsquic_stream_t *stream, lsquic_stream_ctx_t *ctx) {
  ProxyPair *pair = (ProxyPair *)ctx;
  if (pair) {
    if (!pair->closed) {
      close(pair->local_fd);
    }
    fd_to_proxy.erase(pair->local_fd);
    stream_to_proxy.erase(stream);
    delete pair;
  }
}

static void client_on_goaway_received(lsquic_conn_t *conn) {
  LSQ_NOTICE("GOAWAY received");
}

static const lsquic_stream_if stream_callbacks = {
    .on_new_conn = client_on_new_conn,
    .on_goaway_received = client_on_goaway_received,
    .on_conn_closed = client_on_conn_closed,
    .on_new_stream = client_on_new_stream,
    .on_read = client_on_read,
    .on_write = client_on_write,
    .on_close = client_on_close,
    .on_dg_write = NULL,
    .on_datagram = NULL,
    .on_hsk_done = client_on_hsk_done,
    .on_new_token = NULL,
    .on_sess_resume_info = NULL,
    .on_reset = client_on_reset,
    .on_conncloseframe_received = client_on_conncloseframe_received,
};

// Create UDP socket for client (bound to ephemeral port)
int create_client_udp_socket() {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0)
    return -1;
  struct sockaddr_in sa = {};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(0);
  sa.sin_addr.s_addr = INADDR_ANY;
  if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    close(s);
    return -1;
  }
  // Enable broadcast to allow sending to 255.255.255.255
  int opt = 1;
  if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
    perror("setsockopt SO_BROADCAST");
    close(s);
    return -1;
  }
  return s;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " server_ip" << std::endl;
    return EXIT_FAILURE;
  }
  const char *server_ip = argv[1];

  // Initialize LSQUIC logger for debugging
  lsquic_logger_init(&logger_if, NULL, LLTS_HHMMSSUS);
  lsquic_set_log_level("debug");

  if (lsquic_global_init(LSQUIC_GLOBAL_CLIENT) != 0) {
    std::cerr << "LSQUIC init failed" << std::endl;
    return EXIT_FAILURE;
  }

  // Initialize OpenSSL
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();

  client_ssl_ctx = SSL_CTX_new(TLS_method());
  if (!client_ssl_ctx) {
    std::cerr << "SSL_CTX_new failed" << std::endl;
    ERR_print_errors_fp(stderr);
    return EXIT_FAILURE;
  }

  // Load server's self-signed cert as trusted CA
  if (SSL_CTX_load_verify_locations(
          client_ssl_ctx,
          "/home/randolph/projects/applications/quic-rproxy/certs/cert.pem",
          NULL) != 1) {

    std::cerr << "Failed to load CA certificate (cert.pem)" << std::endl;
    ERR_print_errors_fp(stderr);
    SSL_CTX_free(client_ssl_ctx);
    return EXIT_FAILURE;
  }

  // Set verify with custom callback for debugging
  SSL_CTX_set_verify(client_ssl_ctx, SSL_VERIFY_PEER, verify_callback);

  lsquic_engine_settings es = {};
  lsquic_engine_init_settings(&es, 0);
  es.es_idle_conn_to = IDLE_TIMEOUT_US;

  lsquic_engine_api engine_api = {};
  engine_api.ea_settings = &es;
  engine_api.ea_stream_if = &stream_callbacks;
  engine_api.ea_packets_out = send_packets_out;
  engine_api.ea_alpn = alpn;
  engine_api.ea_get_ssl_ctx = get_client_ssl_ctx;

  quic_udp_fd = create_client_udp_socket();
  if (quic_udp_fd < 0) {
    std::cerr << "create_client_udp_socket failed" << std::endl;
    return EXIT_FAILURE;
  }
  engine_api.ea_packets_out_ctx = (void *)(uintptr_t)quic_udp_fd;

  engine = lsquic_engine_new(0, &engine_api);
  if (!engine) {
    std::cerr << "LSQUIC engine new failed" << std::endl;
    return EXIT_FAILURE;
  }

  struct sockaddr_in local_sa = {}, peer_sa = {};
  local_sa.sin_family = AF_INET;
  // Get bound port if needed
  socklen_t len = sizeof(local_sa);
  getsockname(quic_udp_fd, (struct sockaddr *)&local_sa, &len);

  peer_sa.sin_family = AF_INET;
  peer_sa.sin_port = htons(4433);
  peer_sa.sin_addr.s_addr = inet_addr(server_ip);

  // Use server_ip as SNI to potentially fix hostname mismatch
  if (lsquic_engine_connect(
          engine, LSQVER_ID29, (struct sockaddr *)&local_sa,
          (struct sockaddr *)&peer_sa, (void *)(uintptr_t)quic_udp_fd, nullptr,
          server_ip, strlen(server_ip), nullptr, 0, nullptr, 0) == nullptr) {
    std::cerr << "Connect failed" << std::endl;
    return EXIT_FAILURE;
  }

  std::vector<struct pollfd> pollfds;
  pollfds.reserve(MAX_POLL_FDS);
  pollfds.push_back({quic_udp_fd, POLLIN, 0});

  while (true) {
    int timeout_ms = -1;
    int diff;
    if (lsquic_engine_earliest_adv_tick(engine, &diff)) {
      timeout_ms = (diff > 0) ? diff / 1000 : 0;
    }

    pollfds.resize(1);
    size_t idx = 1;
    for (auto &p : fd_to_proxy) {
      short events = p.second->closed ? 0 : POLLIN;
      if (!p.second->to_local_buf.empty())
        events |= POLLOUT;
      if (events != 0) {
        if (idx >= MAX_POLL_FDS)
          break;
        pollfds[idx++] = {p.first, events, 0};
      }
    }
    pollfds.resize(idx);

    int ret = poll(pollfds.data(), pollfds.size(), timeout_ms);
    if (ret < 0) {
      perror("poll");
      continue;
    }

    if (pollfds[0].revents & POLLIN) {
      char buf[0x1000];
      struct sockaddr_storage from_ss;
      socklen_t flen = sizeof(from_ss);
      ssize_t n = recvfrom(quic_udp_fd, buf, sizeof(buf), 0,
                           (struct sockaddr *)&from_ss, &flen);
      if (n > 0) {
        struct sockaddr_storage local_ss;
        socklen_t local_len = sizeof(local_ss);
        getsockname(quic_udp_fd, (struct sockaddr *)&local_ss, &local_len);
        lsquic_engine_packet_in(
            engine, (unsigned char *)buf, n, (struct sockaddr *)&local_ss,
            (struct sockaddr *)&from_ss, (void *)(uintptr_t)quic_udp_fd, 0);
      }
    }

    // Handle local fds
    idx = 1;
    auto now = std::chrono::steady_clock::now();
    for (auto it = fd_to_proxy.begin(); it != fd_to_proxy.end();) {
      if (idx >= pollfds.size())
        break;
      struct pollfd &pfd = pollfds[idx++];
      ProxyPair *pair = it->second;
      if (pair->closed && pair->to_local_buf.empty()) {
        close(pair->local_fd);
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
      if ((pfd.revents & POLLOUT) && !pair->to_local_buf.empty()) {
        ssize_t written =
            write(pfd.fd, pair->to_local_buf.data(), pair->to_local_buf.size());
        if (written > 0) {
          pair->last_activity = now;
          pair->to_local_buf.erase(pair->to_local_buf.begin(),
                                   pair->to_local_buf.begin() + written);
        } else if (written < 0) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            pair->closed = true;
            lsquic_stream_close(pair->stream);
          }
        }
      }
      // Timeout
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

  if (client_ssl_ctx) {
    SSL_CTX_free(client_ssl_ctx);
  }
  EVP_cleanup();
  lsquic_engine_destroy(engine);
  lsquic_global_cleanup();
  return EXIT_SUCCESS;
}

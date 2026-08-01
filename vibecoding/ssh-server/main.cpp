// exossh.cpp
// Copyright 2026 Randolph Ledesma
// Licensed under the Apache License, Version 2.0

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <print>
#include <random>
#include <string>
#include <string_view>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <uv.h>
#include <prometheus/prometheus.h>

using namespace std::literals;
using namespace prometheus;

constexpr uint16_t PORT                    = 2222;
constexpr uint16_t METRICS_PORT            = 9100;
constexpr uint64_t INTERVAL_LINE_BANNER_MS = 10'000;
constexpr size_t   MAX_LINE_LENGTH         = 32;
constexpr size_t   READ_BUFFER_SIZE        = 1024;
constexpr int      MAX_BACKLOG             = 64;

constexpr std::string_view VERSION = "1.3.0";

constexpr std::array<uint8_t, 12> PROXY_V2_SIG = {
    0x0d, 0x0a, 0x0d, 0x0a, 0x00, 0x0d,
    0x0a, 0x51, 0x55, 0x49, 0x54, 0x0a
};

struct Metrics {
    registry_t registry;

    counter_metric_t connections_total{registry, "connections_total",
                                       "Total accepted connections"};
    gauge_metric_t   connections_active{registry, "connections_active",
                                        "Currently active connections"};
    counter_metric_t bytes_sent_total{registry, "bytes_sent_total",
                                      "Total bytes sent to clients"};
    counter_metric_t banners_sent_total{registry, "banners_sent_total",
                                        "Total random banner lines sent"};

    family_t proxy_headers{registry, "proxy_headers_total",
                           "PROXY protocol header parsing results"};
    counter_metric_t proxy_ok{proxy_headers, {{"result", "ok"}}};
    counter_metric_t proxy_malformed{proxy_headers, {{"result", "malformed"}}};
    counter_metric_t proxy_none{proxy_headers, {{"result", "none"}}};

    info_metric_t build_info{registry, "build_info", "Build information",
                             {{"version", std::string(VERSION)},
                              {"component", "exossh"}}};
};

using MetricsPtr = std::shared_ptr<Metrics>;

class BannerGenerator {
public:
    explicit BannerGenerator(uint64_t seed = 0)
        : eng_(seed ? seed : std::random_device{}()) {}

    // Never starts with "SSH-", always ends with \r\n
    std::string next(size_t maxlen = MAX_LINE_LENGTH) {
        const size_t len = 3 + dist_(eng_) % (maxlen - 2);
        std::string line(len, '\0');

        for (size_t i = 0; i < len - 2; ++i)
            line[i] = static_cast<char>(32 + dist_(eng_) % 95);

        line[len - 2] = '\r';
        line[len - 1] = '\n';

        if (line.starts_with("SSH-"))
            line[0] = 'X';

        return line;
    }

private:
    std::mt19937_64 eng_;
    std::uniform_int_distribution<unsigned> dist_{0, 0xffff};
};

int parse_proxy_v1(std::string_view data, char* out_ip, size_t out_ip_len) {
    if (data.size() < 8 || !data.starts_with("PROXY "))
        return 0;

    const auto nl = data.find('\n');
    if (nl == std::string_view::npos)
        return 0;

    const size_t hdr_len = nl + 1;
    if (hdr_len > 108)
        return -1;

    std::string hdr(data.substr(0, hdr_len - 1));

    char proto[8]{}, src[INET6_ADDRSTRLEN]{}, dst[INET6_ADDRSTRLEN]{};
    int src_port = 0, dst_port = 0;

    if (std::sscanf(hdr.c_str(), "PROXY %7s %45s %45s %d %d",
                    proto, src, dst, &src_port, &dst_port) != 5) {
        if (hdr.starts_with("PROXY UNKNOWN")) {
            std::strncpy(out_ip, "unknown", out_ip_len - 1);
            out_ip[out_ip_len - 1] = '\0';
            return static_cast<int>(hdr_len);
        }
        return -1;
    }

    if (std::strcmp(proto, "TCP4") != 0 && std::strcmp(proto, "TCP6") != 0)
        return -1;

    std::strncpy(out_ip, src, out_ip_len - 1);
    out_ip[out_ip_len - 1] = '\0';
    return static_cast<int>(hdr_len);
}

int parse_proxy_v2(const uint8_t* data, size_t len, char* out_ip, size_t out_ip_len) {
    if (len < 16)
        return 0;
    if (std::memcmp(data, PROXY_V2_SIG.data(), 12) != 0)
        return 0;

    const uint8_t ver_cmd   = data[12];
    const uint8_t fam_proto = data[13];
    const uint16_t addr_len = static_cast<uint16_t>((data[14] << 8) | data[15]);
    const size_t total      = 16 + addr_len;

    if (len < total)
        return 0;
    if ((ver_cmd & 0xf0) != 0x20)
        return -1;

    const uint8_t cmd = ver_cmd & 0x0f;
    if (cmd != 0x00 && cmd != 0x01)
        return -1;

    if (cmd == 0x00) {
        std::strncpy(out_ip, "unknown", out_ip_len - 1);
        out_ip[out_ip_len - 1] = '\0';
        return static_cast<int>(total);
    }

    const uint8_t family = (fam_proto >> 4) & 0x0f;

    if (family == 0x01) {
        if (addr_len < 12)
            return -1;
        in_addr src{};
        std::memcpy(&src, data + 16, 4);
        if (!inet_ntop(AF_INET, &src, out_ip, static_cast<socklen_t>(out_ip_len)))
            return -1;
    } else if (family == 0x02) {
        if (addr_len < 36)
            return -1;
        in6_addr src{};
        std::memcpy(&src, data + 16, 16);
        if (!inet_ntop(AF_INET6, &src, out_ip, static_cast<socklen_t>(out_ip_len)))
            return -1;
    } else if (family == 0x00) {
        std::strncpy(out_ip, "unknown", out_ip_len - 1);
        out_ip[out_ip_len - 1] = '\0';
    } else {
        return -1;
    }

    return static_cast<int>(total);
}

int try_parse_proxy(const char* data, size_t len, char* out_ip, size_t out_ip_len) {
    int n = parse_proxy_v2(reinterpret_cast<const uint8_t*>(data), len, out_ip, out_ip_len);
    if (n != 0)
        return n;
    return parse_proxy_v1({data, len}, out_ip, out_ip_len);
}

struct Client {
    uv_tcp_t      handle{};
    uv_timer_t    timer{};
    uv_write_t    write_req{};
    std::array<char, READ_BUFFER_SIZE> read_buf{};

    char          ip_address[INET6_ADDRSTRLEN]{"unknown"};
    bool          proxy_done = false;
    int           refcount   = 2;          // tcp + timer

    BannerGenerator rng;
    MetricsPtr      metrics;
    std::string     banner;
};

void on_close(uv_handle_t* h) {
    auto* client = static_cast<Client*>(h->data);
    if (--client->refcount == 0) {
        if (client->metrics)
            --client->metrics->connections_active;
        std::println("{} disconnected", client->ip_address);
        delete client;
    }
}

void alloc_buffer(uv_handle_t* h, size_t /*suggested*/, uv_buf_t* buf) {
    auto* client = static_cast<Client*>(h->data);
    buf->base = client->read_buf.data();
    buf->len  = client->read_buf.size();
}

void on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    auto* client = static_cast<Client*>(stream->data);

    if (nread < 0) {
        if (nread != UV_EOF)
            std::println(stderr, "Client read error: {}", uv_err_name(static_cast<int>(nread)));
        uv_close(reinterpret_cast<uv_handle_t*>(&client->handle), on_close);
        uv_close(reinterpret_cast<uv_handle_t*>(&client->timer),  on_close);
        return;
    }

    if (nread == 0)
        return;

    if (!client->proxy_done) {
        int consumed = try_parse_proxy(buf->base, static_cast<size_t>(nread),
                                       client->ip_address, sizeof(client->ip_address));

        if (consumed > 0) {
            std::println("{} connected (via PROXY)", client->ip_address);
            client->proxy_done = true;
            if (client->metrics)
                ++client->metrics->proxy_ok;
            return;
        }
        if (consumed < 0) {
            std::println(stderr, "Malformed PROXY header from {}", client->ip_address);
            if (client->metrics)
                ++client->metrics->proxy_malformed;
            uv_close(reinterpret_cast<uv_handle_t*>(&client->handle), on_close);
            uv_close(reinterpret_cast<uv_handle_t*>(&client->timer),  on_close);
            return;
        }

        client->proxy_done = true;
        if (client->metrics)
            ++client->metrics->proxy_none;
    }
}

void on_write(uv_write_t* req, int status) {
    auto* client = static_cast<Client*>(req->handle->data);

    if (status < 0) {
        std::println(stderr, "Write error: {}", uv_err_name(status));
        uv_close(reinterpret_cast<uv_handle_t*>(&client->handle), on_close);
        uv_close(reinterpret_cast<uv_handle_t*>(&client->timer),  on_close);
        return;
    }

    uv_timer_start(&client->timer,
                   [](uv_timer_t* t) {
                       auto* c = static_cast<Client*>(t->data);
                       c->banner = c->rng.next();
                       uv_buf_t b = uv_buf_init(c->banner.data(),
                                                static_cast<unsigned>(c->banner.size()));

                       int r = uv_write(&c->write_req,
                                        reinterpret_cast<uv_stream_t*>(&c->handle),
                                        &b, 1, on_write);
                       if (r < 0) {
                           std::println(stderr, "uv_write failed: {}", uv_err_name(r));
                           uv_close(reinterpret_cast<uv_handle_t*>(&c->handle), on_close);
                           uv_close(reinterpret_cast<uv_handle_t*>(&c->timer),  on_close);
                       } else if (c->metrics) {
                           c->metrics->bytes_sent_total += c->banner.size();
                           ++c->metrics->banners_sent_total;
                       }
                   },
                   INTERVAL_LINE_BANNER_MS, 0);
}

void on_new_connection(uv_stream_t* server, int status) {
    if (status < 0) {
        std::println(stderr, "New connection error: {}", uv_err_name(status));
        return;
    }

    auto* metrics = static_cast<Metrics*>(server->data);
    auto  client  = std::make_unique<Client>();

    // Non-owning view; Metrics outlives all clients
    client->metrics = MetricsPtr{metrics, [](Metrics*){}};

    uv_tcp_init(uv_default_loop(), &client->handle);
    uv_timer_init(uv_default_loop(), &client->timer);

    client->handle.data = client.get();
    client->timer.data  = client.get();

    if (uv_accept(server, reinterpret_cast<uv_stream_t*>(&client->handle)) != 0) {
        uv_close(reinterpret_cast<uv_handle_t*>(&client->handle), on_close);
        uv_close(reinterpret_cast<uv_handle_t*>(&client->timer),  on_close);
        return;
    }

    sockaddr_storage addr{};
    int namelen = sizeof(addr);
    if (uv_tcp_getpeername(&client->handle, reinterpret_cast<sockaddr*>(&addr), &namelen) == 0) {
        if (addr.ss_family == AF_INET) {
            auto* a = reinterpret_cast<sockaddr_in*>(&addr);
            uv_ip4_name(a, client->ip_address, sizeof(client->ip_address));
        } else if (addr.ss_family == AF_INET6) {
            auto* a = reinterpret_cast<sockaddr_in6*>(&addr);
            uv_ip6_name(a, client->ip_address, sizeof(client->ip_address));
        }
    }

    std::println("{} connected", client->ip_address);

    uv_os_fd_t fd;
    if (uv_fileno(reinterpret_cast<uv_handle_t*>(&client->handle), &fd) == 0) {
        int val = 1024;
        setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_RCVBUF, &val, sizeof(val));
        val = 1;
        setsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    }
    uv_tcp_nodelay(&client->handle, 1);

    if (metrics) {
        ++metrics->connections_total;
        ++metrics->connections_active;
    }

    uv_read_start(reinterpret_cast<uv_stream_t*>(&client->handle), alloc_buffer, on_read);

    uv_timer_start(&client->timer,
                   [](uv_timer_t* t) {
                       auto* c = static_cast<Client*>(t->data);
                       c->banner = c->rng.next();
                       uv_buf_t b = uv_buf_init(c->banner.data(),
                                                static_cast<unsigned>(c->banner.size()));
                       int r = uv_write(&c->write_req,
                                        reinterpret_cast<uv_stream_t*>(&c->handle),
                                        &b, 1, on_write);
                       if (r < 0) {
                           std::println(stderr, "uv_write failed: {}", uv_err_name(r));
                           uv_close(reinterpret_cast<uv_handle_t*>(&c->handle), on_close);
                           uv_close(reinterpret_cast<uv_handle_t*>(&c->timer),  on_close);
                       } else if (c->metrics) {
                           c->metrics->bytes_sent_total += c->banner.size();
                           ++c->metrics->banners_sent_total;
                       }
                   },
                   INTERVAL_LINE_BANNER_MS, 0);

    client.release();
}

int main() {
    auto metrics = std::make_shared<Metrics>();

    http_server_t metrics_server(metrics->registry,
                                 std::format("0.0.0.0:{}", METRICS_PORT));

    uv_loop_t* loop = uv_default_loop();

    uv_tcp_t server{};
    uv_tcp_init(loop, &server);
    server.data = metrics.get();

    sockaddr_in6 addr{};
    uv_ip6_addr("::", PORT, &addr);
    uv_tcp_bind(&server, reinterpret_cast<const sockaddr*>(&addr), 0);

    int r = uv_listen(reinterpret_cast<uv_stream_t*>(&server), MAX_BACKLOG, on_new_connection);
    if (r) {
        std::println(stderr, "Listen error: {}", uv_err_name(r));
        return 1;
    }

    std::println("exossh {}", VERSION);
    std::println("Listening on [::]:{}", PORT);
    std::println("Listening on 0.0.0.0:{}", PORT);
    std::println("Metrics available at http://0.0.0.0:{}/metrics", METRICS_PORT);

    uv_run(loop, UV_RUN_DEFAULT);

    uv_close(reinterpret_cast<uv_handle_t*>(&server), nullptr);
    uv_loop_close(loop);
    return 0;
}

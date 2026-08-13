#include <boost/range/irange.hpp>
#include <csignal>
#include <seastar/core/abort_source.hh>
#include <seastar/core/app-template.hh>
#include <seastar/core/condition-variable.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/metrics.hh>
#include <seastar/core/metrics_registration.hh>
#include <seastar/core/prometheus.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/signal.hh>
#include <seastar/coroutine/parallel_for_each.hh>
#include <seastar/http/httpd.hh>
#include <seastar/net/api.hh>
#include <seastar/net/inet_address.hh>
#include <seastar/util/log.hh>

using namespace seastar;
namespace bpo = boost::program_options;

seastar::logger applog("tcp_service");

namespace app_lib {

class stop_signal {
  bool _caught = false;
  seastar::condition_variable _cond;

  void signaled() {
    if (_caught) {
      return;
    }
    _caught = true;
    _cond.broadcast();
  }

public:
  stop_signal() {
    seastar::handle_signal(SIGINT, [this] { signaled(); });
    seastar::handle_signal(SIGTERM, [this] { signaled(); });
  }

  future<> wait() {
    return _cond.wait([this] { return _caught; });
  }

  bool stopping() const { return _caught; }
};

} // namespace app_lib

class tcp_service {
  server_socket _listener;
  seastar::gate _gate;
  seastar::abort_source _abort;
  uint64_t _active_connections = 0;
  uint64_t _total_connections = 0;
  seastar::metrics::metric_groups _metrics;

  void setup_metrics() {
    namespace sm = seastar::metrics;
    _metrics.add_group(
        "tcp_service",
        {
            sm::make_gauge(
                "active_connections", [this] { return _active_connections; },
                sm::description("Currently open connections")),
            sm::make_counter("total_connections", _total_connections,
                             sm::description("Total connections accepted")),
        });
  }

  future<> handle_connection(connected_socket sock) {
    ++_active_connections;
    ++_total_connections;
    auto in = sock.input();
    auto out = sock.output();
    try {
      while (true) {
        auto buf = co_await in.read();
        if (buf.empty()) {
          break;
        }
        co_await out.write(std::move(buf));
        co_await out.flush();
      }
    } catch (...) {
      applog.warn("connection error: {}", std::current_exception());
    }
    co_await out.close();
    --_active_connections;
  }

  future<> accept_loop() {
    while (!_abort.abort_requested()) {
      try {
        auto ar = co_await _listener.accept();
        (void)with_gate(_gate,
                        [this, sock = std::move(ar.connection)]() mutable {
                          return handle_connection(std::move(sock));
                        });
      } catch (...) {
        if (!_abort.abort_requested()) {
          applog.warn("accept failed: {}", std::current_exception());
        }
        break;
      }
    }
  }

public:
  future<> start(uint16_t port) {
    setup_metrics();
    listen_options lo;
    lo.reuse_address = true;
    _listener = seastar::listen(make_ipv4_address({port}), lo);
    (void)with_gate(_gate, [this] { return accept_loop(); });
    co_return;
  }

  future<> stop() {
    _abort.request_abort();
    _listener.abort_accept();
    co_await _gate.close();
  }

  uint64_t total_connections() const { return _total_connections; }
};

sharded<tcp_service> tcp_svc;

future<uint64_t> aggregate_total_connections() {
  uint64_t sum = 0;
  co_await coroutine::parallel_for_each(
      boost::irange<unsigned>(0, smp::count),
      [&sum](unsigned shard) -> future<> {
        foreign_ptr<shared_ptr<uint64_t>> val =
            co_await tcp_svc.invoke_on(shard, [](tcp_service &svc) {
              return make_foreign(
                  make_shared<uint64_t>(svc.total_connections()));
            });
        sum += *val;
      });
  co_return sum;
}

int main(int argc, char **argv) {
  app_template app;
  app.add_options()("port", bpo::value<uint16_t>()->default_value(1234),
                    "TCP port")("prometheus-port",
                                bpo::value<uint16_t>()->default_value(9180),
                                "Prometheus port");

  return app.run(argc, argv, [&app]() -> future<> {
    app_lib::stop_signal signal;

    auto port = app.configuration()["port"].as<uint16_t>();
    auto prom_port = app.configuration()["prometheus-port"].as<uint16_t>();

    co_await tcp_svc.start();
    co_await tcp_svc.invoke_on_all(&tcp_service::start, port);

    httpd::http_server_control prometheus_server;
    prometheus::config pctx;
    pctx.prefix = "myapp";
    co_await prometheus_server.start("prometheus");
    co_await prometheus::start(prometheus_server, pctx);
    co_await prometheus_server.listen(
        socket_address{net::inet_address("0.0.0.0"), prom_port});

    applog.info("listening: tcp {}, metrics :{}/metrics", port, prom_port);

    co_await signal.wait();

    applog.info("shutting down...");
    co_await prometheus_server.stop();
    co_await tcp_svc.stop();
  });
}

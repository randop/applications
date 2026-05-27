#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/sleep.hh>
#include <seastar/coroutine/maybe_yield.hh>
#include <seastar/http/handlers.hh>
#include <seastar/http/httpd.hh>
#include <seastar/http/reply.hh>
#include <seastar/http/routes.hh>
#include <seastar/util/closeable.hh>
#include <seastar/util/log.hh>

#include <memory>

namespace http = seastar::http;
namespace httpd = seastar::httpd;

static seastar::logger logger("app");

class hello_handler : public seastar::httpd::handler_base {
public:
  seastar::future<std::unique_ptr<seastar::http::reply>>
  handle(const seastar::sstring &path,
         std::unique_ptr<seastar::http::request> req,
         std::unique_ptr<seastar::http::reply> rep) override {
    (void)req;
    logger.info("handle: {}", path);
    rep->set_status(seastar::http::reply::status_type::ok);
    rep->add_header("Content-Type", "text/plain");
    rep->write_body("txt", "Hello");
    return seastar::make_ready_future<std::unique_ptr<seastar::http::reply>>(
        std::move(rep));
  }
};

class lookup_handler : public seastar::httpd::handler_base {
public:
  seastar::future<std::unique_ptr<seastar::http::reply>>
  handle(const seastar::sstring &path,
         std::unique_ptr<seastar::http::request> req,
         std::unique_ptr<seastar::http::reply> rep) override {
    (void)req;
    logger.info("lookup: {}", path);
    auto id = req->param.at("id");
    rep->set_status(seastar::http::reply::status_type::ok);
    rep->add_header("Content-Type", "text/plain");
    rep->write_body("txt", seastar::format("lookup id={}\n", id));
    return seastar::make_ready_future<std::unique_ptr<seastar::http::reply>>(
        std::move(rep));
  }
};

class http_service {
  seastar::httpd::http_server _server;

public:
  http_service() : _server("api") {}

  seastar::future<> start(uint16_t port) {
    _server._routes.add(httpd::operation_type::GET, seastar::httpd::url("/"),
                        new hello_handler());
    _server._routes.add(httpd::operation_type::GET,
                        seastar::httpd::url("/lookup").remainder("id"),
                        new lookup_handler());
    _server._routes.add(seastar::httpd::GET, seastar::httpd::url(".*"),
                        new hello_handler());
    _server._routes.add_default_handler(new hello_handler());

    seastar::listen_options opts;
    opts.reuse_address = true;
    opts.lba = seastar::server_socket::load_balancing_algorithm::port;

    co_await _server.listen(seastar::make_ipv4_address(port), opts);
    logger.info("shard {}: listening on :{}", seastar::this_shard_id(), port);
  }

  seastar::future<> stop() { co_await _server.stop(); }
};

int main(int argc, char **argv) {
  seastar::app_template app;
  return app.run(argc, argv, [] -> seastar::future<> {
    auto svc = std::make_shared<seastar::sharded<http_service>>();
    co_await svc->start();
    co_await svc->invoke_on_all(&http_service::start, uint16_t(8080));

    logger.info("HTTP server up on {} shards", seastar::smp::count);
    while (true) {
      co_await seastar::coroutine::maybe_yield();
      co_await seastar::sleep(std::chrono::seconds(1));
    }

    co_await svc->stop();
  });
}

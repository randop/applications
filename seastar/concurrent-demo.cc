#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/future.hh>
#include <seastar/core/reactor.hh>
#include <seastar/net/api.hh>
#include <seastar/util/log.hh>

#include <exception>
#include <utility>

static seastar::logger applog("concurrent-demo");

seastar::future<> handle_connection(seastar::connected_socket cs,
                                    seastar::socket_address remote) {
  applog.info("+ connection from {}", remote);
  auto in = cs.input();
  auto out = cs.output();
  try {
    while (true) {
      auto buf = co_await in.read();
      if (buf.empty()) {
        break;
      }
      co_await out.write(std::move(buf));
      co_await out.flush();
    }
    applog.info("- connection from {} closed cleanly", remote);
  } catch (const std::exception &ex) {
    applog.warn("! connection from {} error: {}", remote, ex.what());
  }
  co_await out.close();
}

seastar::future<> serve(uint16_t port) {
  seastar::listen_options opts;
  opts.reuse_address = true;
  auto ss = seastar::listen(seastar::make_ipv4_address({port}), opts);
  applog.info("Echo server shard {} listening on 0.0.0.0:{}",
              seastar::this_shard_id(), port);
  while (true) {
    auto ar = co_await ss.accept();
    auto addr = ar.remote_address;
    (void)handle_connection(std::move(ar.connection), addr)
        .handle_exception([=](std::exception_ptr ep) {
          try {
            std::rethrow_exception(ep);
          } catch (const std::exception &ex) {
            applog.error("Unhandled error for {}: {}", addr, ex.what());
          }
        });
  }
}

int main(int argc, char **argv) {
  seastar::app_template app;
  namespace po = boost::program_options;
  app.add_options()("port,p", po::value<uint16_t>()->default_value(8080),
                    "TCP port to listen on");
  return app.run(argc, argv, [&app]() -> seastar::future<> {
    uint16_t port = app.configuration()["port"].as<uint16_t>();
    return seastar::smp::invoke_on_all([port] { return serve(port); });
  });
}

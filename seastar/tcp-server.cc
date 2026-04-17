#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/file.hh>
#include <seastar/core/future-util.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/timer-set.hh>
#include <seastar/net/api.hh>
#include <seastar/util/log.hh>
#include <seastar/util/tmp_file.hh>

#include "stop_signal.hh"

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <random>
#include <utility>

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0

#define STRINGIFY0(s) #s
#define STRINGIFY(s) STRINGIFY0(s)

#define VERSION_STRING                                                         \
  STRINGIFY(VERSION_MAJOR)                                                     \
  "." STRINGIFY(VERSION_MINOR) "." STRINGIFY(VERSION_PATCH)

static seastar::logger applog("tcp-server");

seastar::future<> handle_connection(seastar::connected_socket cs,
                                    seastar::socket_address remote,
                                    uint32_t timeout_seconds) {
  applog.info("+ connection from {}", remote);
  return seastar::tmp_dir::do_with(
      [cs = std::move(cs), remote,
       timeout_seconds](seastar::tmp_dir &t) mutable -> seastar::future<> {
        auto in = cs.input();
        auto out = cs.output();
        seastar::timer<> idle_timer;
        bool active = true;
        idle_timer.set_callback([&, remote] {
          active = false;
          applog.info("Client {} idle timeout", remote);
        });
        idle_timer.arm(std::chrono::seconds(timeout_seconds));
        auto seed = std::chrono::system_clock::now().time_since_epoch().count();
        auto rnd = std::mt19937(seed);
        seastar::sstring client_filename =
            (t.get_path() / std::to_string(rnd())).native();
        auto f = co_await seastar::open_file_dma(
            client_filename,
            seastar::open_flags::rw | seastar::open_flags::create);
        applog.info("Writing to {} for client {}", client_filename, remote);
        try {
          size_t offset = 0;
          while (active) {
            auto buf = co_await in.read();
            if (buf.empty()) {
              break;
            }
            idle_timer.rearm(seastar::timer<>::clock::now() +
                             std::chrono::seconds(timeout_seconds));
            co_await f.dma_write(offset, buf.get(), buf.size());
            offset += buf.size();
          }
          applog.info("Finished writing {} bytes to {} for client {}", offset,
                      client_filename, remote);
        } catch (const std::exception &ex) {
          applog.warn("! connection from {} error: {}", remote, ex.what());
        }
        idle_timer.cancel();
        co_await f.close();
        co_await out.close();
        applog.info("Client {} connection finished", remote);
      });
}

seastar::future<> serve(uint16_t port,
                        const seastar_apps_lib::stop_signal &stop_signal,
                        seastar::gate &gate, uint32_t timeout_seconds) {
  seastar::listen_options opts;
  opts.reuse_address = true;
  auto ss = seastar::listen(seastar::make_ipv4_address({port}), opts);
  applog.info("Echo server shard {} listening on 0.0.0.0:{}",
              seastar::this_shard_id(), port);
  seastar::timer<> timer;
  uint64_t connection_count = 0;
  timer.set_callback([&connection_count, &timer] {
    applog.info("Active connections on shard {}: {}", seastar::this_shard_id(),
                connection_count);
    timer.arm(std::chrono::seconds(10));
  });
  timer.arm(std::chrono::seconds(10));
  seastar::timer<> abort_timer;
  abort_timer.set_callback([&ss, &stop_signal] {
    if (stop_signal.stopping()) {
      ss.abort_accept();
    }
  });
  abort_timer.arm_periodic(std::chrono::milliseconds(100));
  while (!stop_signal.stopping()) {
    try {
      auto ar = co_await ss.accept();
      auto addr = ar.remote_address;
      gate.enter();
      connection_count++;
      (void)handle_connection(std::move(ar.connection), addr, timeout_seconds)
          .handle_exception([=](std::exception_ptr ep) {
            try {
              std::rethrow_exception(ep);
            } catch (const std::exception &ex) {
              applog.error("Unhandled error for {}: {}", addr, ex.what());
            }
          })
          .finally([&gate, &connection_count] {
            connection_count--;
            gate.leave();
          });
    } catch (const std::exception &ex) {
      if (!stop_signal.stopping()) {
        applog.error("Accept failed: {}", ex.what());
      }
    }
  }
  abort_timer.cancel();
  ss.abort_accept();
  applog.info("Waiting for connections to finish on shard {}",
              seastar::this_shard_id());
  co_await gate.close();
  applog.info("Server shard {} stopped", seastar::this_shard_id());
}

int main(int argc, char **argv) {
  std::cout << VERSION_STRING << std::endl;
  seastar::app_template app;
  namespace po = boost::program_options;
  app.add_options()("port,p", po::value<uint16_t>()->default_value(8080),
                    "TCP port to listen on")(
      "timeout,t", po::value<uint32_t>()->default_value(5),
      "Client idle timeout in seconds");
  return app.run(argc, argv, [&app]() -> seastar::future<> {
    uint16_t port = app.configuration()["port"].as<uint16_t>();
    uint32_t timeout_seconds = app.configuration()["timeout"].as<uint32_t>();
    seastar::sharded<seastar::gate> gate;
    co_await gate.start();
    auto stop_signal = std::make_shared<seastar_apps_lib::stop_signal>();
    auto f = seastar::smp::invoke_on_all(
        [port, stop_signal, &gate, timeout_seconds] {
          return serve(port, *stop_signal, gate.local(), timeout_seconds);
        });
    co_await stop_signal->wait();
    applog.info("Signal received, stopping servers");
    co_await std::move(f);
    co_await gate.stop();
  });
}

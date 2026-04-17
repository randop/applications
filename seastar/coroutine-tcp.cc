#include "stop_signal.hh"
#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/file.hh>
#include <seastar/core/fstream.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/queue.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/sleep.hh>
#include <seastar/net/api.hh>
#include <seastar/util/log.hh>

using namespace seastar;

logger applog("app");

future<> file_writer(lw_shared_ptr<file> file_ptr,
                     lw_shared_ptr<queue<temporary_buffer<char>>> q) {
  size_t offset = 0;
  try {
    while (true) {
      auto buf = co_await q->pop_eventually();
      co_await file_ptr->dma_write(offset, buf.get(), buf.size());
      offset += buf.size();
      applog.info("Data written to file");
    }
  } catch (const std::exception &e) {
    applog.error("File writer error: {}", e.what());
  } catch (...) {
    applog.error("Unknown error in file writer");
  }

  try {
    co_await file_ptr->close();
    applog.info("File writer: file closed.");
  } catch (...) {
    applog.error("Error closing file");
  }
  co_return;
}

future<> handle_connection(connected_socket s, socket_address addr,
                           lw_shared_ptr<queue<temporary_buffer<char>>> q) {
  try {
    auto in = s.input();

    while (true) {
      temporary_buffer<char> buf = co_await in.read();
      if (buf.empty()) {
        applog.info("Client from {} disconnected.", addr);
        break;
      }

      applog.info("Received {} bytes from {} → queued for DMA file", buf.size(),
                  addr);

      co_await q->push_eventually(std::move(buf));
    }

    co_await in.close();
    applog.info("Connection from {} fully closed.", addr);
  } catch (...) {
  }
  co_return;
}

int main(int argc, char **argv) {
  app_template app;

  return app.run(argc, argv, []() -> future<> {
    applog.info("Seastar C++23 coroutine TCP server (v25.05.0)");
    applog.info("Listening on 0.0.0.0:1234");
    applog.info("Safe streaming with graceful shutdown using stop_signal.");

    auto file = co_await open_file_dma("incoming_data.bin",
                                       open_flags::create | open_flags::wo |
                                           open_flags::truncate);

    auto file_ptr = make_lw_shared<seastar::file>(std::move(file));

    auto data_queue = make_lw_shared<queue<temporary_buffer<char>>>(1024);

    seastar_apps_lib::stop_signal stop_signal;

    listen_options lo;
    lo.reuse_address = true;
    auto listener = listen(make_ipv4_address({"0.0.0.0", 1234}), lo);

    auto listener_ptr = make_lw_shared<server_socket>(std::move(listener));

    auto shutdown_handler = [listener_ptr, data_queue]() {
      if (listener_ptr) {
        listener_ptr->abort_accept();
      }
      data_queue->abort(
          std::make_exception_ptr(std::runtime_error("shutdown")));
    };

    handle_signal(SIGINT, shutdown_handler);
    handle_signal(SIGTERM, shutdown_handler);

    co_await do_with(
        std::move(listener_ptr), std::move(file_ptr), std::move(data_queue),
        [&](auto &listener, auto &file_ptr, auto &data_queue) -> future<> {
          gate writer_gate;

          writer_gate.enter();
          file_writer(file_ptr, data_queue)
              .then_wrapped([&writer_gate](auto f) {
                try {
                  f.get();
                } catch (...) {
                }
                writer_gate.leave();
              })
              .discard_result();

          try {
            while (true) {
              accept_result res = co_await listener->accept();

              auto [s, addr] = std::move(res);

              applog.info("Accepted connection from {}", addr);

              co_await handle_connection(std::move(s), std::move(addr),
                                         data_queue);

              applog.info("Handler finished – ready for next connection.");
            }
          } catch (...) {
          }

          co_await writer_gate.close();
          co_return;
        });

    applog.info("Server shutdown complete.");
    co_return;
  });
}

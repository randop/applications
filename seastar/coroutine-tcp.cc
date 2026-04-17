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

// Your stop_signal header
#include "stop_signal.hh"

using namespace std::chrono_literals;

// ======================
// Background writer coroutine
// ======================
seastar::future<> file_writer(
    seastar::lw_shared_ptr<seastar::file> file_ptr,
    seastar::lw_shared_ptr<seastar::queue<seastar::temporary_buffer<char>>> q) {
  size_t offset = 0;
  try {
    while (true) {
      auto buf = co_await q->pop_eventually();
      co_await file_ptr->dma_write(offset, buf.get(), buf.size());
      offset += buf.size();
      std::cout << "Data written to file" << std::endl;
    }
  } catch (const std::exception &e) {
    std::cerr << "File writer error: " << e.what() << '\n';
  } catch (...) {
    std::cerr << "Unknown error in file writer\n";
  }

  try {
    co_await file_ptr->close();
    std::cout << "File writer: file closed.\n";
  } catch (...) {
    std::cerr << "Error closing file\n";
  }
  co_return;
}

// ======================
// Per-connection coroutine
// ======================
seastar::future<> handle_connection(
    seastar::connected_socket s, seastar::socket_address addr,
    seastar::lw_shared_ptr<seastar::queue<seastar::temporary_buffer<char>>> q) {
  try {
    auto in = s.input();

    while (true) {
      seastar::temporary_buffer<char> buf = co_await in.read();
      if (buf.empty()) {
        std::cout << "Client from " << addr << " disconnected.\n";
        break;
      }

      std::cout << "Received " << buf.size() << " bytes from " << addr
                << " → queued for DMA file\n";

      co_await q->push_eventually(std::move(buf));
    }

    co_await in.close();
    std::cout << "Connection from " << addr << " fully closed.\n";
  } catch (...) {
  }
  co_return;
}

int main(int argc, char **argv) {
  seastar::app_template app;

  return app.run(argc, argv, []() -> seastar::future<> {
    std::cout << "Seastar C++23 coroutine TCP server (v25.05.0)\n";
    std::cout << "Listening on 0.0.0.0:1234\n";
    std::cout << "Safe streaming with graceful shutdown using stop_signal.\n\n";

    auto file = co_await seastar::open_file_dma(
        "incoming_data.bin", seastar::open_flags::create |
                                 seastar::open_flags::wo |
                                 seastar::open_flags::truncate);

    auto file_ptr = seastar::make_lw_shared<seastar::file>(std::move(file));

    auto data_queue = seastar::make_lw_shared<
        seastar::queue<seastar::temporary_buffer<char>>>(1024);

    seastar_apps_lib::stop_signal stop_signal;

    seastar::listen_options lo;
    lo.reuse_address = true;
    auto listener =
        seastar::listen(seastar::make_ipv4_address({"0.0.0.0", 1234}), lo);

    // Keep listener in shared_ptr so we can abort it from signal handler
    auto listener_ptr =
        seastar::make_lw_shared<seastar::server_socket>(std::move(listener));

    // Register signal handler that aborts accept + aborts queue
    auto shutdown_handler = [listener_ptr, data_queue]() {
      if (listener_ptr) {
        listener_ptr->abort_accept(); // This wakes up the blocked accept()
      }
      data_queue->abort(
          std::make_exception_ptr(std::runtime_error("shutdown")));
    };

    seastar::handle_signal(SIGINT, shutdown_handler);
    seastar::handle_signal(SIGTERM, shutdown_handler);

    co_await seastar::do_with(
        std::move(listener_ptr), std::move(file_ptr), std::move(data_queue),
        [&](auto &listener, auto &file_ptr,
            auto &data_queue) -> seastar::future<> {
          seastar::gate writer_gate;

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

          // Normal accept loop - will be interrupted by abort_accept()
          try {
            while (true) {
              seastar::accept_result res = co_await listener->accept();

              auto [s, addr] = std::move(res);

              std::cout << "Accepted connection from " << addr << "\n";

              co_await handle_connection(std::move(s), std::move(addr),
                                         data_queue);

              std::cout << "Handler finished – ready for next connection.\n";
            }
          } catch (...) {
            // Accept aborted
          }

          co_await writer_gate.close();
          co_return;
        });

    std::cout << "Server shutdown complete.\n";
    co_return;
  });
}

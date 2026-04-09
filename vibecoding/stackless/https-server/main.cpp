#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <chrono>
#include <iostream>
#include <string>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using ssl_socket = asio::ssl::stream<tcp::socket>;
using namespace boost::asio::experimental::awaitable_operators;

template <typename Op, typename Duration>
asio::awaitable<void> with_timeout(Op &&op, Duration timeout) {
  asio::steady_timer timer(co_await asio::this_coro::executor);
  timer.expires_after(timeout);

  try {
    // Race the operation against a timer using awaitable operators
    co_await (std::forward<Op>(op) || timer.async_wait(asio::use_awaitable));
  } catch (const boost::system::system_error &e) {
    // If the timer wins, async_wait throws operation_aborted
    if (e.code() == asio::error::operation_aborted) {
      throw boost::system::system_error(asio::error::timed_out,
                                        "operation timed out");
    }
    // Re-throw other errors
    throw;
  }
}

// Helper adapters to convert async operations to awaitable<void>
template <typename T>
asio::awaitable<void> as_void(asio::awaitable<T> awaitable_op) {
  co_await std::move(awaitable_op);
  co_return;
}

asio::awaitable<void> handle_connection(ssl_socket stream) {
  try {
    co_await with_timeout(
        as_void(stream.async_handshake(asio::ssl::stream_base::server,
                                       asio::use_awaitable)),
        std::chrono::seconds(15));

    asio::streambuf request_buf;
    co_await with_timeout(
        as_void(asio::async_read_until(stream, request_buf, "\r\n\r\n",
                                       asio::use_awaitable)),
        std::chrono::seconds(30));

    asio::streambuf response_buf;
    std::ostream os(&response_buf);

    os << "HTTP/1.1 200 OK\r\n"
       << "Server: X-Proxy\r\n"
       << "Content-Type: text/plain\r\n"
       << "Content-Length: 5\r\n"
       << "\r\n"
       << "HELLO";

    co_await with_timeout(as_void(asio::async_write(stream, response_buf.data(),
                                                    asio::use_awaitable)),
                          std::chrono::seconds(10));

    co_await with_timeout(as_void(stream.async_shutdown(asio::use_awaitable)),
                          std::chrono::seconds(5));
  } catch (const boost::system::system_error &e) {
    if (e.code() == asio::error::timed_out)
      std::cerr << "Connection timed out\n";
    else
      std::cerr << "Connection error: " << e.what() << '\n';
  } catch (const std::exception &e) {
    std::cerr << "Connection error: " << e.what() << '\n';
  }
}

asio::awaitable<void> do_listen(tcp::acceptor &acceptor,
                                asio::ssl::context &ctx) {
  for (;;) {
    tcp::socket socket(acceptor.get_executor());
    co_await acceptor.async_accept(socket, asio::use_awaitable);

    ssl_socket ssl_stream(std::move(socket), ctx);

    // Fire-and-forget per-connection handler
    asio::co_spawn(acceptor.get_executor(),
                   handle_connection(std::move(ssl_stream)), asio::detached);
  }
}

int main() {
  int port = 8484;

  try {
    asio::io_context io_ctx;

    // TLS context (modern TLSv1.2+)
    asio::ssl::context ssl_ctx(asio::ssl::context::tlsv12_server);
    ssl_ctx.set_options(
        asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 | asio::ssl::context::single_dh_use);

    // Load self-signed certificate and key
    ssl_ctx.use_certificate_file("cert.pem", asio::ssl::context::pem);
    ssl_ctx.use_private_key_file("key.pem", asio::ssl::context::pem);

    tcp::acceptor acceptor(io_ctx, tcp::endpoint(tcp::v4(), port));

    std::cout << "Stackless with coroutines HTTPS server listening on host "
                 "0.0.0.0 port "
              << port << std::endl
              << "Press Ctrl+C to stop." << std::endl;

    // Start the listener coroutine
    asio::co_spawn(io_ctx.get_executor(), do_listen(acceptor, ssl_ctx),
                   asio::detached);

    io_ctx.run();
  } catch (const std::exception &e) {
    std::cerr << "Fatal server error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

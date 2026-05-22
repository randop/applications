#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <iostream>
#include <string>
#include <chrono>
#include <variant>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using ssl_socket = asio::ssl::stream<tcp::socket>;

// =============================================================================
// Timeout wrapper with EXPLICIT cancellation of the operation
// (avoids any possible race condition or memory leak)
// =============================================================================
template<typename Op, typename Duration>
asio::awaitable<void> with_timeout(ssl_socket& stream, Op&& op, Duration timeout)
{
  asio::steady_timer timer(co_await asio::this_coro::executor);
  timer.expires_after(timeout);

  using namespace boost::asio::experimental::awaitable_operators;

  // operator|| races the operation against the timer.
  // When the timer wins, Asio automatically cancels the losing operation.
  // We also do an explicit cancel() for maximum safety (idempotent, no side effects).
  auto raced = std::forward<Op>(op)() || timer.async_wait(asio::use_awaitable);
  auto result = co_await std::move(raced);

  if (result.index() == 1) {                     // timer won the race
    stream.lowest_layer().cancel();              // ← explicit cancel of Op
    throw boost::system::system_error(asio::error::timed_out, "operation timed out");
  }

  // operation completed first → timer is destroyed and auto-cancels itself
  // All local RAII objects (streambufs etc.) are cleaned up by the coroutine frame
}

// =============================================================================
// Per-connection handler (stackless coroutine)
// =============================================================================
asio::awaitable<void> handle_connection(ssl_socket stream)
{
  try
  {
    // TLS handshake – 15 s timeout
    co_await with_timeout(stream,
      [&] { return stream.async_handshake(asio::ssl::stream_base::server,
                                          asio::use_awaitable); },
      std::chrono::seconds(15));

    // Read request headers – 30 s timeout
    asio::streambuf request_buf;
    co_await with_timeout(stream,
      [&] { return asio::async_read_until(stream, request_buf, "\r\n\r\n",
                                          asio::use_awaitable); },
      std::chrono::seconds(30));

    // ====================== TINY HAND-WRITTEN RESPONSE BUILDER ======================
    asio::streambuf response_buf;
    std::ostream os(&response_buf);

    os << "HTTP/1.1 200 OK\r\n"
       << "Server: Boost.Asio-C++20-Coroutine\r\n"
       << "Content-Type: text/plain\r\n"
       << "Content-Length: 13\r\n"
       << "\r\n"
       << "Hello, World!";

    // Write response – 10 s timeout
    co_await with_timeout(stream,
      [&] { return asio::async_write(stream, response_buf.data(),
                                     asio::use_awaitable); },
      std::chrono::seconds(10));
    // =============================================================================

    // Graceful TLS shutdown – 5 s timeout
    co_await with_timeout(stream,
      [&] { return stream.async_shutdown(asio::use_awaitable); },
      std::chrono::seconds(5));
  }
  catch (const boost::system::system_error& e)
  {
    if (e.code() == asio::error::timed_out ||
        e.code() == asio::error::operation_aborted)
      std::cerr << "Connection timed out (or cancelled)\n";
    else
      std::cerr << "Connection error: " << e.what() << '\n';
  }
  catch (const std::exception& e)
  {
    std::cerr << "Connection error: " << e.what() << '\n';
  }
}

// =============================================================================
// Listener coroutine
// =============================================================================
asio::awaitable<void> do_listen(tcp::acceptor& acceptor,
                                asio::ssl::context& ctx)
{
  for (;;)
  {
    tcp::socket socket(acceptor.get_executor());
    co_await acceptor.async_accept(socket, asio::use_awaitable);

    ssl_socket ssl_stream(std::move(socket), ctx);

    asio::co_spawn(acceptor.get_executor(),
                   handle_connection(std::move(ssl_stream)),
                   asio::detached);
  }
}

// =============================================================================
// Main
// =============================================================================
int main()
{
  try
  {
    asio::io_context io_ctx;

    asio::ssl::context ssl_ctx(asio::ssl::context::tlsv12_server);
    ssl_ctx.set_options(
      asio::ssl::context::default_workarounds |
      asio::ssl::context::no_sslv2 |
      asio::ssl::context::no_sslv3 |
      asio::ssl::context::single_dh_use);

    ssl_ctx.use_certificate_file("cert.pem", asio::ssl::context::pem);
    ssl_ctx.use_private_key_file("key.pem", asio::ssl::context::pem);

    tcp::acceptor acceptor(io_ctx, tcp::endpoint(tcp::v4(), 8443));

    std::cout << "Boost.Asio HTTPS server (C++20 coroutines + explicit timeout cancellation) "
                 "listening on https://localhost:8443\n"
              << "Press Ctrl+C to stop.\n";

    asio::co_spawn(io_ctx.get_executor(),
                   do_listen(acceptor, ssl_ctx),
                   asio::detached);

    io_ctx.run();
  }
  catch (const std::exception& e)
  {
    std::cerr << "Fatal server error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}

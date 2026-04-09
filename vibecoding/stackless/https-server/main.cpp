#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <iostream>
#include <string>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using ssl_socket = asio::ssl::stream<tcp::socket>;

asio::awaitable<void> handle_connection(ssl_socket stream) {
  try {
    // TLS handshake
    co_await stream.async_handshake(asio::ssl::stream_base::server,
                                    asio::use_awaitable);

    asio::streambuf request_buf;
    co_await asio::async_read_until(stream, request_buf, "\r\n\r\n",
                                    asio::use_awaitable);

    // Fixed HTTP response
    const std::string response = "HTTP/1.1 200 OK\r\n"
                                 "Server: X-Proxy\r\n"
                                 "Content-Type: text/plain\r\n"
                                 "Content-Length: 5\r\n"
                                 "\r\n"
                                 "Hello";

    co_await asio::async_write(stream, asio::buffer(response),
                               asio::use_awaitable);

    // Graceful TLS shutdown
    co_await stream.async_shutdown(asio::use_awaitable);
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

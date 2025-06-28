#include "httpServer.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

namespace App {

class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
  HttpConnection(net::ip::tcp::socket socket, ModSecurityFilter &filter)
      : socket_(std::move(socket)), filter_(filter), buffer_(8192) {}

  void Start() { ReadRequest(); }

private:
  net::ip::tcp::socket socket_;
  ModSecurityFilter &filter_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> request_;
  http::response<http::string_body> response_;

  void ReadRequest() {
    auto self = shared_from_this();
    http::async_read(socket_, buffer_, request_,
                     [self](beast::error_code ec, std::size_t) {
                       if (!ec)
                         self->ProcessRequest();
                     });
  }

  void ProcessRequest() {
    response_.version(request_.version());
    response_.keep_alive(false);

    auto client_ip = socket_.remote_endpoint().address().to_string();
    if (!filter_.ProcessRequest(request_.method_string(), request_.target(),
                                request_.body(), client_ip)) {
      response_.result(http::status::forbidden);
      response_.set(http::field::content_type, "text/plain");
      response_.body() = "Request blocked by OWASP CRS";
    } else {
      response_.result(http::status::ok);
      response_.set(http::field::content_type, "text/plain");
      response_.body() = "Request processed successfully";
    }

    WriteResponse();
  }

  void WriteResponse() {
    auto self = shared_from_this();
    response_.content_length(response_.body().size());
    http::async_write(
        socket_, response_, [self](beast::error_code ec, std::size_t) {
          self->socket_.shutdown(net::ip::tcp::socket::shutdown_send, ec);
        });
  }
};

HttpServer::HttpServer(net::io_context &ioc, StringView address,
                       unsigned short port)
    : acceptor_(ioc), socket_(ioc), filter_() {
  net::ip::tcp::endpoint endpoint(net::ip::make_address(address), port);
  acceptor_.open(endpoint.protocol());
  acceptor_.set_option(net::socket_base::reuse_address(true));
  acceptor_.bind(endpoint);
  acceptor_.listen(net::socket_base::max_listen_connections);
}

void HttpServer::Start() { Accept(); }

void HttpServer::Accept() {
  acceptor_.async_accept(socket_, [this](beast::error_code ec) {
    if (!ec) {
      auto conn = std::make_shared<HttpConnection>(std::move(socket_), filter_);
      conn->Start();
    }
    Accept();
  });
}

} // namespace App
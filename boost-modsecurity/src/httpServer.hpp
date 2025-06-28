#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#include "common.hpp"
#include "modSecurityFilter.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace App {

class HttpServer {
public:
  HttpServer(boost::asio::io_context &ioc, StringView address,
             unsigned short port);
  void Start();

private:
  boost::asio::ip::tcp::acceptor acceptor_;
  boost::asio::ip::tcp::socket socket_;
  ModSecurityFilter filter_;

  void Accept();
};

} // namespace App

#endif // HTTP_SERVER_HPP
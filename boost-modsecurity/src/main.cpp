#include "httpServer.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <thread>

int main(int argc, char *argv[]) {
  try {
    const auto num_threads = std::max(1u, std::thread::hardware_concurrency());
    const std::string address = "0.0.0.0";
    const unsigned short port = 8080;

    boost::asio::io_context ioc{static_cast<int>(num_threads)};
    App::HttpServer server(ioc, address, port);
    server.Start();
    std::cout << "Server running on " << address << " port " << port
              << std::endl;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (auto i = num_threads; i > 0; --i) {
      threads.emplace_back([&ioc] { ioc.run(); });
    }

    for (auto &thread : threads) {
      thread.join();
    }
  } catch (const std::exception &e) {
    std::cerr << "main Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
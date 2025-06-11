/***
###############################################################################
# Includes
###############################################################################
***/
#include "include/connectionPool.hpp"
#include "include/dbConnection.hpp"
#include "include/httpServer.hpp"
#include "include/post.hpp"

#include <cmark.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

/***
###############################################################################
# Constants
###############################################################################
***/
#include "include/constants.h"

int main(int argc, char **argv) {
  std::cout << "Blog server project: 1.0" << std::endl;

  const char *host = "0.0.0.0";
  auto const address = net::ip::make_address(host);
  auto const port = static_cast<unsigned short>(DEFAULT_PORT);
  auto const docRoot = std::make_shared<std::string>("/tmp");
  auto const threadCount = std::max<int>(1, 4);

  int postId = 1;

  try {
    // Initialize connection pool
    auto pool = std::make_shared<db::ConnectionPool>(
        "dbname=mydb user=myuser password=mypassword "
        "host=192.168.100.3 port=5432",
        MAX_DB_CONNECTION);

    // Get a connection from the pool
    auto conn = pool->getConnection();
    if (conn) {
      std::cout << "DB pool connection: OK" << std::endl;
      pqxx::work txn(*conn);
      auto result =
          txn.exec("SELECT id, created_at, updated_at, title, content, mode_id "
                   "FROM posts WHERE id = $1 LIMIT 1",
                   postId);

      int modeId = MODE_MARKDOWN;
      const char *markdown;
      for (const auto &row : result) {
        std::cout << "ID: " << row.at("id").as<int>()
                  << ", created: " << row.at("created_at").c_str()
                  << ", updated: " << row.at("updated_at").c_str() << std::endl
                  << "title: " << row.at("title").c_str() << std::endl
                  << "content: " << std::endl;
        modeId = row.at("mode_id").as<int>();
        if (modeId == MODE_MARKDOWN) {
          markdown = row.at("content").c_str();
          auto html = std::unique_ptr<char, void (*)(void *)>(
              cmark_markdown_to_html(markdown, strlen(markdown),
                                     CMARK_OPT_DEFAULT),
              std::free);
          std::cout << html.get() << std::endl;
        } else if (modeId == MODE_HTML) {
          std::cout << row.at("content").c_str() << std::endl;
        }
      }
      txn.commit();
      pool->releaseConnection(conn);
    } else {
      std::cerr << "Failed to get connection from pool" << std::endl;
    }

    auto post = std::make_shared<blog::Post>(pool);

    // The io_context is required for all I/O
    net::io_context ioc{threadCount};

    // Create and launch a listening port
    std::make_shared<listener>(ioc, tcp::endpoint{address, port}, docRoot, post)
        ->run();

    spdlog::info("http server listening on {} port {}", host, port);

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> threads;
    threads.reserve(threadCount - 1);
    for (auto i = threadCount - 1; i > 0; --i) {
      threads.emplace_back([&ioc] { ioc.run(); });
    }
    ioc.run();
  } catch (const std::exception &e) {
    spdlog::error("Error: {}", e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

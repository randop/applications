#include "include/connectionPool.hpp"
#include "include/dbConnection.hpp"
#include <cmark.h>
#include <iostream>
#include <memory>
#include <string_view>

const int MAX_DB_CONNECTION = 10;

int main(int argc, char **argv) {
  std::cout << "Blog server project: 1.0" << std::endl;

  int postId = 1;

  try {
    // Initialize connection pool
    auto pool =
        db::ConnectionPool("dbname=mydb user=myuser password=mypassword "
                           "host=192.168.100.3 port=5432",
                           MAX_DB_CONNECTION);

    // Get a connection from the pool
    auto conn = pool.getConnection();
    if (conn) {
      std::cout << "DB pool connection: OK" << std::endl;
      pqxx::work txn(*conn);
      auto result =
          txn.exec("SELECT * FROM posts WHERE id = $1 LIMIT 1", postId);
      for (const auto &row : result) {
        std::cout << "ID: " << row[0].as<int>()
                  << ", Title: " << row[1].as<std::string>() << "\n";
      }
      txn.commit();
      pool.releaseConnection(conn);
    } else {
      std::cerr << "Failed to get connection from pool" << std::endl;
    }

    // Convert Markdown to HTML
    const char *markdown = "# Hello, World!\nThis is a **Markdown** example.";
    auto html = std::unique_ptr<char, void (*)(void *)>(
        cmark_markdown_to_html(markdown, strlen(markdown), CMARK_OPT_DEFAULT),
        std::free);
    std::cout << "Markdown to HTML:\n" << html.get() << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

#include "include/connectionPool.hpp"
#include "include/dbConnection.hpp"
#include <iostream>
#include <memory>
#include <string_view>

const int MAX_DB_CONNECTION = 10;

int main(int argc, char **argv) {
  std::cout << "Blog server project: 1.0" << std::endl;

  try {
    // Initialize connection pool
    auto pool = db::ConnectionPool(
        "dbname=mydb user=myuser password=mypassword host=192.168.100.3 port=5432",
        MAX_DB_CONNECTION);

    // Get a connection from the pool
    auto conn = pool.getConnection();
    if (conn) {
      pqxx::work txn(*conn);
      std::cout << "DB pool connection: OK" << std::endl;
      pool.releaseConnection(conn);
    } else {
      std::cerr << "Failed to get connection from pool" << std::endl;
    }

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

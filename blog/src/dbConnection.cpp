#include "dbConnection.hpp"

namespace db {

const int RECONNECT_BACKOFF = 2000;

Connection::Connection(const std::string &connStr) {
  connectionString = connStr;
  try {
    conn = std::make_shared<pqxx::connection>(connStr);
    if (!conn->is_open()) {
      throw std::runtime_error("Failed to open database connection");
    }
  } catch (const std::exception &e) {
    spdlog::error("Connection error: {}", e.what());
    throw std::runtime_error("Database connection error");
  }
}

std::shared_ptr<pqxx::connection> Connection::get() { return conn; }

} // namespace db

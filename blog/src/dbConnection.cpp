#include "dbConnection.hpp"
#include <stdexcept>

namespace db {
Connection::Connection(const std::string& connStr) {
  try {
    conn = std::make_shared<pqxx::connection>(connStr);
    if (!conn->is_open()) {
      throw std::runtime_error("Failed to open database connection");
    }
  } catch (const std::exception& e) {
    throw std::runtime_error("Connection error: " + std::string(e.what()));
  }
}

std::shared_ptr<pqxx::connection> Connection::get() const {
  return conn;
}
}

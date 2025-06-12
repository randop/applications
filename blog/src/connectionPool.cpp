#include "connectionPool.hpp"

namespace db {
ConnectionPool::ConnectionPool(const std::string &connStr, size_t poolSize)
    : connStr(connStr), poolSize(poolSize) {
  for (size_t i = 0; i < poolSize; ++i) {
    connections.push(std::make_shared<Connection>(connStr));
  }
}

ConnectionPool::~ConnectionPool() {
  std::unique_lock<std::mutex> lock(mutex);
  while (!connections.empty()) {
    connections.pop();
  }
}

std::shared_ptr<pqxx::connection> ConnectionPool::getConnection() {
  std::unique_lock<std::mutex> lock(mutex);
  auto timeout = std::chrono::steady_clock::now() +
                 std::chrono::seconds(POOL_CONNECT_TIMEOUT);
  condVar.wait_until(lock, timeout, [this] { return !connections.empty(); });
  if (connections.empty()) {
    lock.unlock();
    condVar.notify_one();
    throw std::runtime_error("Database pool empty error");
  }
  auto conn = connections.front();
  connections.pop();
  lock.unlock();
  return conn->get();
}

void ConnectionPool::releaseConnection(std::shared_ptr<pqxx::connection> conn) {
  std::unique_lock<std::mutex> lock(mutex);
  connections.push(std::make_shared<Connection>(connStr));
  lock.unlock();
  condVar.notify_one();
}

void ConnectionPool::releaseAll() {
  std::unique_lock<std::mutex> lock(mutex);
  while (!connections.empty()) {
    connections.pop();
  }

  try {
    for (size_t i = 0; i < poolSize; ++i) {
      connections.push(std::make_shared<Connection>(connStr));
    }
  } catch (const std::exception &e) {
    spdlog::error("Connection release all error: {}", e.what());

    while (!connections.empty()) {
      connections.pop();
    }
  }

  lock.unlock();
  condVar.notify_all();
}

} // namespace db

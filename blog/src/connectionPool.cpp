#include "connectionPool.hpp"
#include <stdexcept>

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
  condVar.wait(lock, [this] { return !connections.empty(); });

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
} // namespace db

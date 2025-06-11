#ifndef BLOG_CONNECTION_POOL_H
#define BLOG_CONNECTION_POOL_H

#include "dbConnection.hpp"
#include <condition_variable>
#include <mutex>
#include <queue>

namespace db {
class ConnectionPool {
public:
  ConnectionPool(const std::string &connStr, size_t poolSize);
  ~ConnectionPool();

  std::shared_ptr<pqxx::connection> getConnection();
  void releaseConnection(std::shared_ptr<pqxx::connection> conn);

private:
  std::string connStr;
  size_t poolSize;
  std::queue<std::shared_ptr<Connection>> connections;
  std::mutex mutex;
  std::condition_variable condVar;
};
} // namespace db

#endif // BLOG_CONNECTION_POOL_H

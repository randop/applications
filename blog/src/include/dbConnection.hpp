#pragma once

#ifndef BLOG_DB_CONNECTION_H
#define BLOG_DB_CONNECTION_H

#include <memory>
#include <pqxx/pqxx>
#include <spdlog/spdlog.h>

#include <chrono>
#include <stdexcept>
#include <thread>

namespace db {

class Connection {
public:
  explicit Connection(const std::string &connStr);
  std::shared_ptr<pqxx::connection> get();

private:
  std::shared_ptr<pqxx::connection> conn;
  std::string connectionString;
};

} // namespace db

#endif // BLOG_DB_CONNECTION_H

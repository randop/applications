#pragma once

#ifndef BLOG_DB_CONNECTION_H
#define BLOG_DB_CONNECTION_H

#include <memory>
#include <pqxx/pqxx>

namespace db {
class Connection {
public:
  explicit Connection(const std::string &connStr);
  std::shared_ptr<pqxx::connection> get() const;

private:
  std::shared_ptr<pqxx::connection> conn;
};
} // namespace db

#endif // BLOG_DB_CONNECTION_H

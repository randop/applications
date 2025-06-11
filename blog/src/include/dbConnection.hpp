#ifndef BLOG_DATABASE_DB_CONNECTION_H
#define BLOG_DATABASE_DB_CONNECTION_H

#include <pqxx/pqxx>
#include <memory>

namespace db {
class Connection {
public:
  explicit Connection(const std::string& connStr);
  std::shared_ptr<pqxx::connection> get() const;

private:
  std::shared_ptr<pqxx::connection> conn;
};
} // namespace db

#endif // BLOG_DATABASE_DB_CONNECTION_H

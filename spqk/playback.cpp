#include "playback.hpp"

#include <boost/asio.hpp>
#include <sqlite3.h>

#include <future>
#include <functional>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>

namespace asio = boost::asio;
using asio::awaitable;
using asio::use_awaitable;

namespace {

class PlaybackManager {
 public:
  PlaybackManager(asio::thread_pool& database_workers, const std::string& path)
      : database_workers_(database_workers) {
    if (sqlite3_open(path.c_str(), &database_) != SQLITE_OK) {
      const std::string error = database_ ? sqlite3_errmsg(database_) : "open failed";
      if (database_) sqlite3_close(database_);
      database_ = nullptr;
      throw std::runtime_error("cannot open payload database: " + error);
    }
    execute("CREATE TABLE IF NOT EXISTS payloads("
            "id INTEGER PRIMARY KEY, data BLOB, status TEXT, created_at TEXT)");
    execute("UPDATE payloads SET status='pending' WHERE status='playing'");
  }

  ~PlaybackManager() { if (database_) sqlite3_close(database_); }

  awaitable<void> store_payload(const std::string& data) {
    co_await asio::post(database_workers_, use_awaitable);
    sqlite3_stmt* statement = nullptr;
    check(sqlite3_prepare_v2(database_,
        "INSERT INTO payloads(data,status,created_at) VALUES(?1,'pending',datetime('now'))",
        -1, &statement, nullptr));
    sqlite3_bind_blob(statement, 1, data.data(), static_cast<int>(data.size()), SQLITE_TRANSIENT);
    const int result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    check(result == SQLITE_DONE ? SQLITE_OK : result);
  }

  awaitable<std::optional<std::pair<sqlite3_int64, std::string>>> take_next() {
    co_await asio::post(database_workers_, use_awaitable);
    sqlite3_stmt* statement = nullptr;
    check(sqlite3_prepare_v2(database_,
        "SELECT id,data FROM payloads WHERE status='pending' ORDER BY id LIMIT 1",
        -1, &statement, nullptr));
    const int result = sqlite3_step(statement);
    if (result == SQLITE_DONE) {
      sqlite3_finalize(statement);
      co_return std::nullopt;
    }
    check(result);
    const auto id = sqlite3_column_int64(statement, 0);
    const auto* blob = static_cast<const char*>(sqlite3_column_blob(statement, 1));
    const int size = sqlite3_column_bytes(statement, 1);
    std::string data(blob ? blob : "", static_cast<std::size_t>(size));
    sqlite3_finalize(statement);
    check(sqlite3_prepare_v2(database_, "UPDATE payloads SET status='playing' WHERE id=?1",
                            -1, &statement, nullptr));
    sqlite3_bind_int64(statement, 1, id);
    const int update_result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    check(update_result == SQLITE_DONE ? SQLITE_OK : update_result);
    co_return std::make_optional(std::make_pair(id, std::move(data)));
  }

  awaitable<void> erase(sqlite3_int64 id) {
    co_await asio::post(database_workers_, use_awaitable);
    sqlite3_stmt* statement = nullptr;
    check(sqlite3_prepare_v2(database_, "DELETE FROM payloads WHERE id=?1", -1,
                            &statement, nullptr));
    sqlite3_bind_int64(statement, 1, id);
    const int result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    check(result == SQLITE_DONE ? SQLITE_OK : result);
  }

  awaitable<void> mark_failed(sqlite3_int64 id) {
    co_await asio::post(database_workers_, use_awaitable);
    sqlite3_stmt* statement = nullptr;
    check(sqlite3_prepare_v2(database_, "UPDATE payloads SET status='failed' WHERE id=?1",
                            -1, &statement, nullptr));
    sqlite3_bind_int64(statement, 1, id);
    const int result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    check(result == SQLITE_DONE ? SQLITE_OK : result);
  }

 private:
  void check(int result) const {
    if (result != SQLITE_OK && result != SQLITE_ROW && result != SQLITE_DONE) {
      throw std::runtime_error(sqlite3_errmsg(database_));
    }
  }
  void execute(const char* sql) {
    char* error = nullptr;
    const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
      const std::string message = error ? error : sqlite3_errmsg(database_);
      sqlite3_free(error);
      throw std::runtime_error(message);
    }
  }

  sqlite3* database_ = nullptr;
  asio::thread_pool& database_workers_;
};

awaitable<int> process_pending_speech(PlaybackManager& manager, const std::string& data,
                                      std::function<int(const std::string&)> speak) {
  co_await manager.store_payload(data);
  int result = 0;
  while (auto payload = co_await manager.take_next()) {
    std::exception_ptr failure;
    try {
      result = speak(payload->second);
    } catch (...) {
      failure = std::current_exception();
    }
    if (failure) {
      co_await manager.mark_failed(payload->first);
      std::rethrow_exception(failure);
    }
    if (result == 0) co_await manager.erase(payload->first);
    else co_await manager.mark_failed(payload->first);
    if (result != 0) break;
  }
  co_return result;
}

}  // namespace

int process_speech(const std::string& payload,
                   std::function<int(const std::string&)> speak) {
  asio::io_context context;
  auto strand = asio::make_strand(context);
  asio::thread_pool database_workers(1);
  PlaybackManager manager(database_workers, "spqk.sqlite3");
  auto completion = asio::co_spawn(strand,
                                   process_pending_speech(manager, payload, std::move(speak)),
                                   asio::use_future);
  context.run();
  database_workers.join();
  return completion.get();
}

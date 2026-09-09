// slack_socket_mode.cpp
//
// Slack Socket Mode bot using Boost.Beast + Boost.Asio.
//
// This version uses an Asio strand for the entire WebSocket session.
// All WebSocket reads, writes, timers, and connection state transitions are
// asynchronous and serialized through the same strand.
//
// Build (Ubuntu 24.04 / Boost 1.90):
//   apt-get install libboost-system-dev libssl-dev
//   g++ -std=c++17 -O2 slack_socket_mode.cpp -o slack_socket_mode \
//       -lboost_system -lssl -lcrypto -lpthread
//
// Run:
//   export SLACK_BOT_TOKEN=xoxb-...
//   export SLACK_APP_TOKEN=xapp-...
//   ./slack_socket_mode

#define BOOST_JSON_NO_LIB

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/json/src.hpp>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace json = boost::json;
using tcp = net::ip::tcp;

namespace {

constexpr bool APP_DEBUG_MESSAGES = false;

void log(const std::string &msg) {
  std::cerr << "[slack] " << msg << std::endl;
}

bool is_equals(const std::string &a, const std::string &b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [](unsigned char c1, unsigned char c2) {
                      return std::tolower(c1) == std::tolower(c2);
                    });
}

bool wildcardMatch(const std::string &str, const std::string &pattern) {
  size_t s = 0, p = 0;
  size_t star = std::string::npos, sMark = 0;

  while (s < str.size()) {
    if (p < pattern.size() && pattern[p] == str[s]) {
      ++s;
      ++p;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      sMark = s;
    } else if (star != std::string::npos) {
      p = star + 1;
      s = ++sMark;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') {
    ++p;
  }
  return p == pattern.size();
}

const json::object kEmptyObject;

std::string getStr(const json::object &obj, std::string_view key,
                   std::string def = "") {
  if (auto *p = obj.if_contains(key); p && p->is_string()) {
    return std::string(p->as_string());
  }
  return def;
}

bool getBool(const json::object &obj, std::string_view key, bool def = false) {
  if (auto *p = obj.if_contains(key); p && p->is_bool()) {
    return p->as_bool();
  }
  return def;
}

bool has(const json::object &obj, std::string_view key) {
  return obj.if_contains(key) != nullptr;
}

const json::object &getObj(const json::object &obj, std::string_view key) {
  if (auto *p = obj.if_contains(key); p && p->is_object()) {
    return p->as_object();
  }
  return kEmptyObject;
}

// This is intentionally synchronous because it is used from the HTTP worker
// pool, never from the WebSocket strand.
json::value httpsPostJson(const std::string &host, const std::string &target,
                          const std::string &bearerToken,
                          const json::value &body) {
  net::io_context ioc;
  ssl::context ctx{ssl::context::tlsv12_client};
  ctx.set_default_verify_paths();
  ctx.set_verify_mode(ssl::verify_peer);

  tcp::resolver resolver{ioc};
  auto const results = resolver.resolve(host, "443");

  ssl::stream<tcp::socket> stream{ioc, ctx};
  if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
    throw beast::system_error(beast::error_code(
        static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()));
  }

  net::connect(stream.next_layer(), results.begin(), results.end());
  stream.handshake(ssl::stream_base::client);

  http::request<http::string_body> req{http::verb::post, target, 11};
  req.set(http::field::host, host);
  req.set(http::field::user_agent, "boost-beast-slack-bot/1.0");
  req.set(http::field::authorization, "Bearer " + bearerToken);
  req.set(http::field::content_type, "application/json; charset=utf-8");
  req.body() = json::serialize(body);
  req.prepare_payload();

  http::write(stream, req);

  beast::flat_buffer buffer;
  http::response<http::string_body> res;
  http::read(stream, buffer, res);

  beast::error_code ec;
  stream.shutdown(ec);

  return json::parse(res.body());
}

struct WssUrl {
  std::string host;
  std::string target;
};

WssUrl parseWssUrl(const std::string &url) {
  const std::string prefix = "wss://";
  if (url.rfind(prefix, 0) != 0) {
    throw std::runtime_error("expected wss:// url, got: " + url);
  }

  std::string rest = url.substr(prefix.size());
  auto slashPos = rest.find('/');

  WssUrl out;
  if (slashPos == std::string::npos) {
    out.host = rest;
    out.target = "/";
  } else {
    out.host = rest.substr(0, slashPos);
    out.target = rest.substr(slashPos);
  }
  return out;
}

// Ask Slack for a fresh Socket Mode WebSocket URL.
std::string openSocketModeUrl(const std::string &appToken) {
  json::value res = httpsPostJson("slack.com", "/api/apps.connections.open",
                                  appToken, json::value(json::object{}));

  const json::object &obj = res.as_object();
  if (!getBool(obj, "ok")) {
    throw std::runtime_error("apps.connections.open failed: " +
                             json::serialize(res));
  }

  return getStr(obj, "url");
}

class SocketModeSession
    : public std::enable_shared_from_this<SocketModeSession> {
  using strand_type = net::strand<net::io_context::executor_type>;
  using websocket_type = websocket::stream<ssl::stream<tcp::socket>>;

  struct PendingWrite {
    std::string data;
  };

public:
  SocketModeSession(net::io_context &ioc, ssl::context &sslCtx,
                    std::string botToken, std::string wsUrl,
                    net::thread_pool &httpPool)
      : strand_(net::make_strand(ioc)), resolver_(strand_),
        ws_(strand_, sslCtx), pingTimer_(strand_),
        botToken_(std::move(botToken)), wsUrl_(std::move(wsUrl)),
        httpPool_(httpPool) {
    parsed_ = parseWssUrl(wsUrl_);
  }

  void start() {
    net::dispatch(strand_,
                  [self = shared_from_this()] { self->startOnStrand(); });
  }

private:
  strand_type strand_;
  tcp::resolver resolver_;
  websocket_type ws_;
  net::steady_timer pingTimer_;

  std::string botToken_;
  std::string wsUrl_;
  WssUrl parsed_;
  net::thread_pool &httpPool_;

  beast::flat_buffer readBuffer_;
  std::deque<PendingWrite> writeQueue_;
  bool writeInProgress_ = false;
  bool reconnect_ = false;
  bool stopped_ = false;
  std::chrono::steady_clock::time_point lastPong_ =
      std::chrono::steady_clock::now();

  static constexpr auto kPingInterval = std::chrono::seconds(30);
  static constexpr auto kPongTimeout = std::chrono::seconds(60);

  void startOnStrand() {
    // Beast invokes this callback while the read operation is active.
    ws_.control_callback([self = shared_from_this()](websocket::frame_type kind,
                                                     beast::string_view) {
      // control_callback is invoked by Beast as part of the
      // WebSocket operation. Marshal state changes explicitly onto
      // the same strand so they remain serialized with timers and
      // writes.
      net::post(self->strand_, [self, kind] {
        if (kind == websocket::frame_type::pong) {
          self->lastPong_ = std::chrono::steady_clock::now();
        }
      });
    });

    resolver_.async_resolve(
        parsed_.host, "443",
        net::bind_executor(strand_, [self = shared_from_this()](
                                        beast::error_code ec,
                                        tcp::resolver::results_type results) {
          self->onResolve(ec, std::move(results));
        }));
  }

  void onResolve(beast::error_code ec, tcp::resolver::results_type results) {
    if (stopped_) {
      return;
    }

    if (ec) {
      fail("DNS resolve", ec);
      return;
    }

    net::async_connect(ws_.next_layer().next_layer(), results,
                       net::bind_executor(strand_, [self = shared_from_this()](
                                                       beast::error_code ec,
                                                       const tcp::endpoint &) {
                         self->onConnect(ec);
                       }));
  }

  void onConnect(beast::error_code ec) {
    if (stopped_) {
      return;
    }

    if (ec) {
      fail("TCP connect", ec);
      return;
    }

    if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(),
                                  parsed_.host.c_str())) {
      beast::error_code sslEc(static_cast<int>(::ERR_get_error()),
                              net::error::get_ssl_category());
      fail("SNI", sslEc);
      return;
    }

    ws_.next_layer().async_handshake(
        ssl::stream_base::client,
        net::bind_executor(strand_,
                           [self = shared_from_this()](beast::error_code ec) {
                             self->onSslHandshake(ec);
                           }));
  }

  void onSslHandshake(beast::error_code ec) {
    if (stopped_) {
      return;
    }

    if (ec) {
      fail("TLS handshake", ec);
      return;
    }

    ws_.set_option(
        websocket::stream_base::decorator([](websocket::request_type &req) {
          req.set(http::field::user_agent, "boost-beast-slack-bot/1.0");
        }));

    ws_.async_handshake(parsed_.host, parsed_.target,
                        net::bind_executor(strand_, [self = shared_from_this()](
                                                        beast::error_code ec) {
                          self->onWebSocketHandshake(ec);
                        }));
  }

  void onWebSocketHandshake(beast::error_code ec) {
    if (stopped_) {
      return;
    }

    if (ec) {
      fail("WebSocket handshake", ec);
      return;
    }

    log("WebSocket handshake complete.");

    // One-off boot notification before entering the read loop.
    sendBootNotification();

    lastPong_ = std::chrono::steady_clock::now();
    schedulePing();
    readNext();
  }

  void sendBootNotification() {
    const char *channelEnv = std::getenv("SLACK_BOOT_CHANNEL");
    if (!channelEnv || !*channelEnv) {
      log("SLACK_BOOT_CHANNEL is not set; skipping boot notification.");
      return;
    }

    const std::string channel = channelEnv;
    const std::string bootId =
        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count());

    json::object message;
    message["channel"] = channel;
    message["text"] = "Boot Notification - Boot ID: " + bootId + " - Welcome!";

    // Use Block Kit for the title/layout and mrkdwn for inline formatting.
    json::array blocks;

    json::object headerText;
    headerText["type"] = "plain_text";
    headerText["text"] = "Boot Notification";
    headerText["emoji"] = true;

    json::object header;
    header["type"] = "header";
    header["text"] = std::move(headerText);
    blocks.push_back(std::move(header));

    json::object sectionText;
    sectionText["type"] = "mrkdwn";
    sectionText["text"] = "- Boot ID: `" + bootId +
                          "`\n"
                          "- Welcome!";

    json::object section;
    section["type"] = "section";
    section["text"] = std::move(sectionText);
    blocks.push_back(std::move(section));

    json::object divider;
    divider["type"] = "divider";
    blocks.push_back(std::move(divider));

    message["blocks"] = std::move(blocks);

    const std::string botToken = botToken_;
    auto weakSelf = weak_from_this();

    // chat.postMessage is blocking, so keep it off the WebSocket strand.
    net::post(httpPool_, [weakSelf, botToken,
                          message = std::move(message)]() mutable {
      try {
        json::value res =
            httpsPostJson("slack.com", "/api/chat.postMessage", botToken,
                          json::value(std::move(message)));

        if (auto self = weakSelf.lock()) {
          net::post(self->strand_, [self, res = std::move(res)]() mutable {
            if (!res.is_object() || !getBool(res.as_object(), "ok")) {
              log("Boot notification failed: " + json::serialize(res));
            }
          });
        }
      } catch (const std::exception &e) {
        log(std::string("Boot notification error: ") + e.what());
      }
    });
  }

  void readNext() {
    if (stopped_) {
      return;
    }

    readBuffer_.clear();

    ws_.async_read(
        readBuffer_,
        net::bind_executor(strand_, [self = shared_from_this()](
                                        beast::error_code ec, std::size_t) {
          self->onRead(ec);
        }));
  }

  void onRead(beast::error_code ec) {
    if (stopped_) {
      return;
    }

    if (ec == websocket::error::closed) {
      log("WebSocket closed by peer.");
      stop();
      return;
    }

    if (ec) {
      fail("WebSocket read", ec);
      return;
    }

    const std::string raw = beast::buffers_to_string(readBuffer_.data());

    json::value envelope;
    try {
      envelope = json::parse(raw);
    } catch (const std::exception &e) {
      log(std::string("Failed to parse envelope JSON: ") + e.what());
      readNext();
      return;
    }

    if (!envelope.is_object()) {
      readNext();
      return;
    }

    handleEnvelope(envelope.as_object());
    readNext();
  }

  void handleEnvelope(const json::object &envelope) {
    const std::string type = getStr(envelope, "type");

    if (type == "hello") {
      log("Socket Mode connection established (hello received).");
      return;
    }

    if (type == "disconnect") {
      log("Received disconnect envelope (reason: " +
          getStr(envelope, "reason", "unknown") + "); reconnecting...");
      reconnect_ = true;
      stop();
      return;
    }

    if (has(envelope, "envelope_id")) {
      json::object ack;
      ack["envelope_id"] = getStr(envelope, "envelope_id");
      queueWrite(json::serialize(json::value(ack)));
    }

    if (type != "events_api") {
      return;
    }

    const json::object &payload = getObj(envelope, "payload");
    const json::object &event = getObj(payload, "event");

    const std::string eventType = getStr(event, "type");
    if (eventType == "message" || eventType == "app_mention") {
      handleMessageEvent(event);
    }
  }

  void handleMessageEvent(const json::object &event) {
    if (has(event, "subtype") || has(event, "bot_id")) {
      return;
    }

    const std::string eventType = getStr(event, "type");

    if (eventType != "app_mention" && getStr(event, "channel_type") != "im") {
      return;
    }

    const std::string text = getStr(event, "text");
    const std::string channel = getStr(event, "channel");
    const std::string ts = getStr(event, "ts");
    const std::string clientMsgId = getStr(event, "client_msg_id");

    const std::string pattern = "<@U****> ping";

    log("Received message: " + clientMsgId);

    json::object reply;
    reply["channel"] = channel;

    bool willReplyOnThread = false;

    if (eventType == "app_mention") {
      willReplyOnThread = false;
    }

    if (APP_DEBUG_MESSAGES) {
      log(boost::json::serialize(event));
    }

    if (willReplyOnThread) {
      reply["thread_ts"] = ts;
    }

    if (is_equals(text, "ping")) {
      reply["text"] = "*pong*";
    } else if (wildcardMatch(text, pattern)) {
      reply["text"] = "*pong*";
    } else {
      reply["text"] = "message `" + clientMsgId + "` acknowledged.";
    }

    const std::string botToken = botToken_;
    auto weakSelf = weak_from_this();

    // The HTTP API call is blocking, so it is deliberately moved off the
    // WebSocket strand. Completion is posted back onto the strand before
    // any WebSocket state is touched.
    net::post(
        httpPool_, [weakSelf, botToken, reply = std::move(reply)]() mutable {
          try {
            json::value res =
                httpsPostJson("slack.com", "/api/chat.postMessage", botToken,
                              json::value(std::move(reply)));

            if (auto self = weakSelf.lock()) {
              net::post(self->strand_, [self, res = std::move(res)]() mutable {
                if (!res.is_object() || !getBool(res.as_object(), "ok")) {
                  log("chat.postMessage failed: " + json::serialize(res));
                }
              });
            }
          } catch (const std::exception &e) {
            if (auto self = weakSelf.lock()) {
              const std::string error = e.what();
              net::post(self->strand_, [self, error] {
                (void)self;
                log("chat.postMessage error: " + error);
              });
            }
          }
        });
  }

  void queueWrite(std::string data) {
    // This function is only called on strand_.
    writeQueue_.push_back(PendingWrite{std::move(data)});
    if (!writeInProgress_) {
      writeNext();
    }
  }

  void writeNext() {
    // This function is only called on strand_.
    if (writeQueue_.empty()) {
      writeInProgress_ = false;
      return;
    }

    writeInProgress_ = true;

    auto &item = writeQueue_.front();

    ws_.async_write(
        net::buffer(item.data),
        net::bind_executor(strand_, [self = shared_from_this()](
                                        beast::error_code ec, std::size_t) {
          self->onWrite(ec);
        }));
  }

  void onWrite(beast::error_code ec) {
    if (stopped_) {
      return;
    }

    if (ec) {
      fail("WebSocket write", ec);
      return;
    }

    writeQueue_.pop_front();
    writeNext();
  }

  void schedulePing() {
    if (stopped_) {
      return;
    }

    pingTimer_.expires_after(std::chrono::seconds(1));
    pingTimer_.async_wait(net::bind_executor(
        strand_, [self = shared_from_this()](beast::error_code ec) {
          self->onPingTimer(ec);
        }));
  }

  void onPingTimer(beast::error_code ec) {
    if (stopped_ || ec == net::error::operation_aborted) {
      return;
    }

    if (ec) {
      fail("ping timer", ec);
      return;
    }

    const auto now = std::chrono::steady_clock::now();

    if (now - lastPong_ > kPongTimeout) {
      log("No pong received within timeout; closing socket to force "
          "reconnect.");
      stop();
      return;
    }

    if (now - lastPingAt_ >= kPingInterval) {
      lastPingAt_ = now;

      // A Beast WebSocket permits a control operation such as ping
      // while an async_read is outstanding. The strand makes the
      // control callback and session state changes serialized.
      ws_.async_ping(
          {}, net::bind_executor(
                  strand_, [self = shared_from_this()](beast::error_code ec) {
                    if (self->stopped_) {
                      return;
                    }
                    if (ec) {
                      self->fail("WebSocket ping", ec);
                    }
                  }));
    }

    schedulePing();
  }

  void fail(const char *where, beast::error_code ec) {
    log(std::string(where) + ": " + ec.message());
    stop();
  }

  void stop() {
    if (stopped_) {
      return;
    }

    stopped_ = true;
    pingTimer_.cancel();
    resolver_.cancel();

    beast::error_code ec;
    beast::get_lowest_layer(ws_).cancel(ec);

    if (ws_.is_open()) {
      ws_.async_close(
          websocket::close_code::normal,
          net::bind_executor(
              strand_, [self = shared_from_this()](beast::error_code closeEc) {
                if (closeEc && closeEc != net::error::operation_aborted) {
                  log("WebSocket close: " + closeEc.message());
                }
              }));
    }
  }

  std::chrono::steady_clock::time_point lastPingAt_ =
      std::chrono::steady_clock::now();
};

} // namespace

int main() {
  const char *botTokenEnv = std::getenv("SLACK_BOT_TOKEN");
  const char *appTokenEnv = std::getenv("SLACK_APP_TOKEN");

  if (!botTokenEnv || !appTokenEnv) {
    std::cerr
        << "Set SLACK_BOT_TOKEN and SLACK_APP_TOKEN environment variables."
        << std::endl;
    return 1;
  }

  const std::string botToken = botTokenEnv;
  const std::string appToken = appTokenEnv;

  net::io_context ioc;
  net::thread_pool httpPool(4);

  ssl::context ctx{ssl::context::tlsv12_client};
  ctx.set_default_verify_paths();
  ctx.set_verify_mode(ssl::verify_peer);

  std::cout << "ArphaXAD is running in socket mode..." << std::endl;

  while (true) {
    try {
      // This is done outside the WebSocket strand. It is a short,
      // one-shot connection used only to obtain the Socket Mode URL.
      const std::string wsUrl = openSocketModeUrl(appToken);

      auto session = std::make_shared<SocketModeSession>(ioc, ctx, botToken,
                                                         wsUrl, httpPool);

      session->start();

      // All WebSocket work is asynchronous. Multiple threads may run
      // this io_context; the session's strand still guarantees that
      // its handlers never execute concurrently.
      ioc.run();
      ioc.restart();
    } catch (const std::exception &e) {
      log(std::string("Session error: ") + e.what());
    }

    log("Reconnecting in 1s...");
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  httpPool.join();
}

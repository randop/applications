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
//   export SLACK_AI_AGENT_TOKEN=...
//   export SLACK_AI_MODEL=moonshotai/kimi-k3
//   # Optional; defaults to OpenAI's Chat Completions endpoint:
//   export SLACK_AI_AGENT_ENDPOINT=https://api.openai.com/v1/chat/completions
//   ./slack_socket_mode

#define APP_VERSION "1.0.0"

#define BOOST_JSON_NO_LIB

#include <boost/asio/async_result.hpp>
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
#include <regex>
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
constexpr bool APP_DEBUG_AI = false;

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

std::string removeSlackMentions(const std::string &text) {
  std::string result = std::regex_replace(text, std::regex(R"(<@[^>]+>)"), "");

  result.erase(0, result.find_first_not_of(" \t\r\n"));
  result.erase(result.find_last_not_of(" \t\r\n") + 1);

  return result;
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

struct HttpsUrl {
  std::string host;
  std::string target;
};

HttpsUrl parseHttpsUrl(const std::string &url) {
  const std::string prefix = "https://";
  if (url.rfind(prefix, 0) != 0) {
    throw std::runtime_error("AI agent endpoint must use https://: " + url);
  }

  const std::string rest = url.substr(prefix.size());
  const auto slashPos = rest.find('/');

  HttpsUrl out;
  if (slashPos == std::string::npos) {
    out.host = rest;
    out.target = "/";
  } else {
    out.host = rest.substr(0, slashPos);
    out.target = rest.substr(slashPos);
  }

  if (out.host.empty()) {
    throw std::runtime_error("AI agent endpoint has an empty host.");
  }
  return out;
}

std::string extractAiAgentReply(const json::value &res) {
  if (!res.is_object()) {
    throw std::runtime_error("AI agent returned a non-object JSON response.");
  }

  const auto &obj = res.as_object();

  // OpenAI Chat Completions compatible response:
  // {"choices":[{"message":{"role":"assistant","content":"..."}}]}
  if (auto *choices = obj.if_contains("choices");
      choices && choices->is_array() && !choices->as_array().empty()) {
    const auto &choice = choices->as_array().front();
    if (choice.is_object()) {
      const auto &choiceObj = choice.as_object();
      if (auto *message = choiceObj.if_contains("message");
          message && message->is_object()) {
        const auto &messageObj = message->as_object();
        if (auto *content = messageObj.if_contains("content");
            content && content->is_string()) {
          return std::string(content->as_string());
        }
      }
    }
  }

  // Some OpenAI-compatible gateways return {"output":"..."}.
  if (auto *output = obj.if_contains("output"); output && output->is_string()) {
    return std::string(output->as_string());
  }

  throw std::runtime_error("AI agent response did not contain an assistant "
                           "message content.");
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

class AiAgentClient : public std::enable_shared_from_this<AiAgentClient> {
  using strand_type = net::strand<net::io_context::executor_type>;
  using ssl_stream_type = ssl::stream<tcp::socket>;

public:
  AiAgentClient(net::io_context &ioc, ssl::context &sslCtx,
                std::string endpoint, std::string token)
      : strand_(net::make_strand(ioc)), resolver_(strand_),
        stream_(strand_, sslCtx), endpoint_(parseHttpsUrl(endpoint)),
        token_(std::move(token)) {}

  template <typename Handler> void query(std::string prompt, Handler handler) {
    net::dispatch(strand_,
                  [self = shared_from_this(), prompt = std::move(prompt),
                   handler = std::move(handler)]() mutable {
                    self->start(std::move(prompt), std::move(handler));
                  });
  }

private:
  strand_type strand_;
  tcp::resolver resolver_;
  ssl_stream_type stream_;
  HttpsUrl endpoint_;
  std::string token_;

  beast::flat_buffer readBuffer_;
  http::request<http::string_body> request_;
  http::response<http::string_body> response_;

  template <typename Handler> void start(std::string prompt, Handler handler) {
    if (token_.empty()) {
      net::post(strand_, [handler = std::move(handler)]() mutable {
        handler(std::make_exception_ptr(
                    std::runtime_error("SLACK_AI_AGENT_TOKEN is empty.")),
                std::string{});
      });
      return;
    }

    std::string aiModel = "moonshotai/kimi-k3";

    // Standard OpenAI Chat Completions request. Streaming is deliberately
    // not enabled: no "stream": true field is sent.
    json::object message;
    message["role"] = "user";
    message["content"] = std::move(prompt);

    json::object systemMessage;
    systemMessage["role"] = "system";
    systemMessage["content"] =
        "You are responding in Slack. "
        "Use Slack mrkdwn syntax, not GitHub Markdown. "
        "Use *bold*, _italic_, ~strikethrough~, `code`, "
        "and <https://example.com|link text>. "
        "Do not use Markdown headings such as # or ##. "
        "Keep formatting compatible with Slack Block Kit mrkdwn.";

    json::array messages;
    messages.push_back(std::move(systemMessage));
    messages.push_back(std::move(message));

    json::object body;
    body["model"] = aiModel;
    body["messages"] = std::move(messages);
    body["stream"] = false;
    body["max_tokens"] = 16384;

    request_ = {};
    request_.version(11);
    request_.method(http::verb::post);
    request_.target(endpoint_.target);
    request_.set(http::field::host, endpoint_.host);
    request_.set(http::field::user_agent, "boost-beast-slack-bot/1.0");
    request_.set(http::field::authorization, "Bearer " + token_);
    request_.set(http::field::content_type, "application/json; charset=utf-8");
    request_.body() = json::serialize(body);
    if (APP_DEBUG_AI) {
      log("ai payload: " + request_.body());
    }
    request_.prepare_payload();

    response_ = {};

    resolver_.async_resolve(
        endpoint_.host, "443",
        net::bind_executor(
            strand_, [self = shared_from_this(), handler = std::move(handler)](
                         beast::error_code ec,
                         tcp::resolver::results_type results) mutable {
              self->onResolve(ec, std::move(results), std::move(handler));
            }));
  }

  template <typename Handler>
  void onResolve(beast::error_code ec, tcp::resolver::results_type results,
                 Handler handler) {
    if (ec) {
      return complete(std::move(handler), ec,
                      "AI agent DNS resolve: " + ec.message());
    }

    net::async_connect(
        stream_.next_layer(), results,
        net::bind_executor(
            strand_,
            [self = shared_from_this(), handler = std::move(handler)](
                beast::error_code connectEc, const tcp::endpoint &) mutable {
              self->onConnect(connectEc, std::move(handler));
            }));
  }

  template <typename Handler>
  void onConnect(beast::error_code ec, Handler handler) {
    if (ec) {
      return complete(std::move(handler), ec,
                      "AI agent TCP connect: " + ec.message());
    }

    if (!SSL_set_tlsext_host_name(stream_.native_handle(),
                                  endpoint_.host.c_str())) {
      const beast::error_code sslEc(static_cast<int>(::ERR_get_error()),
                                    net::error::get_ssl_category());
      return complete(std::move(handler), sslEc,
                      "AI agent SNI: " + sslEc.message());
    }

    stream_.async_handshake(
        ssl::stream_base::client,
        net::bind_executor(
            strand_, [self = shared_from_this(), handler = std::move(handler)](
                         beast::error_code handshakeEc) mutable {
              self->onSslHandshake(handshakeEc, std::move(handler));
            }));
  }

  template <typename Handler>
  void onSslHandshake(beast::error_code ec, Handler handler) {
    if (ec) {
      return complete(std::move(handler), ec,
                      "AI agent TLS handshake: " + ec.message());
    }

    http::async_write(
        stream_, request_,
        net::bind_executor(
            strand_, [self = shared_from_this(), handler = std::move(handler)](
                         beast::error_code writeEc, std::size_t) mutable {
              self->onWrite(writeEc, std::move(handler));
            }));
  }

  template <typename Handler>
  void onWrite(beast::error_code ec, Handler handler) {
    if (ec) {
      return complete(std::move(handler), ec,
                      "AI agent HTTP write: " + ec.message());
    }

    http::async_read(
        stream_, readBuffer_, response_,
        net::bind_executor(
            strand_, [self = shared_from_this(), handler = std::move(handler)](
                         beast::error_code readEc, std::size_t) mutable {
              self->onRead(readEc, std::move(handler));
            }));
  }

  template <typename Handler>
  void onRead(beast::error_code ec, Handler handler) {
    if (ec) {
      return complete(std::move(handler), ec,
                      "AI agent HTTP read: " + ec.message());
    }

    if (response_.result_int() < 200 || response_.result_int() >= 300) {
      const std::string detail = "AI agent HTTP status " +
                                 std::to_string(response_.result_int()) + ": " +
                                 response_.body();
      beast::error_code statusEc = make_error_code(beast::errc::protocol_error);
      return complete(std::move(handler), statusEc, detail);
    }

    try {
      const std::string reply =
          extractAiAgentReply(json::parse(response_.body()));
      shutdown(std::move(handler), reply);
    } catch (const std::exception &e) {
      beast::error_code parseEc =
          make_error_code(beast::errc::invalid_argument);
      complete(std::move(handler), parseEc,
               std::string("AI agent JSON response: ") + e.what());
    }
  }

  template <typename Handler>
  void shutdown(Handler handler, std::string reply) {
    beast::error_code ec;
    stream_.shutdown(ec);
    if (ec == net::error::eof || ec == ssl::error::stream_truncated) {
      ec.clear();
    }

    if (ec) {
      return complete(std::move(handler), ec,
                      "AI agent TLS shutdown: " + ec.message());
    }

    handler(nullptr, std::move(reply));
  }

  template <typename Handler>
  void complete(Handler handler, beast::error_code /*ec*/,
                const std::string &message) {
    beast::error_code ignored;
    stream_.shutdown(ignored);
    handler(std::make_exception_ptr(std::runtime_error(message)),
            std::string{});
  }
};

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
                    net::thread_pool &httpPool, net::io_context &aiIoc,
                    std::string aiEndpoint, std::string aiToken)
      : strand_(net::make_strand(ioc)), resolver_(strand_),
        ws_(strand_, sslCtx), pingTimer_(strand_),
        botToken_(std::move(botToken)), wsUrl_(std::move(wsUrl)),
        httpPool_(httpPool), aiIoc_(aiIoc), sslCtx_(sslCtx),
        aiEndpoint_(std::move(aiEndpoint)), aiToken_(std::move(aiToken)) {
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
  net::io_context &aiIoc_;
  ssl::context &sslCtx_;
  std::string aiEndpoint_;
  std::string aiToken_;

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
    message["text"] = "Boot Notification - Boot ID: " + bootId + " - Ready!!!";

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
    sectionText["text"] = "🚀 Ready";

    json::object section;
    section["type"] = "section";
    section["text"] = std::move(sectionText);

    json::array fields;

    json::object bootField;
    bootField["type"] = "mrkdwn";
    bootField["text"] = "ℹ️ Boot ID: `" + bootId + "`";
    fields.push_back(std::move(bootField));

    json::object versionField;
    versionField["type"] = "mrkdwn";
    versionField["text"] = "ℹ️ Version: `" APP_VERSION "`";
    fields.push_back(std::move(versionField));

    json::object systemField;
    systemField["type"] = "mrkdwn";
    systemField["text"] = "🖥️ System: `Linux`";
    fields.push_back(std::move(systemField));

    section["fields"] = std::move(fields);

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

    if (text.empty() || channel.empty()) {
      log("Ignoring message with empty text or channel.");
      return;
    }

    log("Received message: " + clientMsgId);

    if (APP_DEBUG_MESSAGES) {
      log(boost::json::serialize(event));
    }

    // Query the AI agent asynchronously on its own io_context. The Slack
    // WebSocket strand is never blocked waiting for the model response.
    const std::string botToken = botToken_;
    const std::string prompt = removeSlackMentions(text);
    const std::string replyChannel = channel;
    const std::string replyThread = ts;
    bool replyOnThread = false;
    auto weakSelf = weak_from_this();

    bool isPing = false;
    const std::string pattern = "<@U****> ping";
    if (is_equals(text, "ping") || wildcardMatch(text, pattern)) {
      isPing = true;
    }

    if (isPing) {
      json::object reply;
      reply["channel"] = replyChannel;
      reply["text"] = "*pong*";

      if (replyOnThread && !replyThread.empty()) {
        reply["thread_ts"] = replyThread;
      }

      // Slack chat.postMessage is still blocking, so keep it off
      // the WebSocket strand on the existing HTTP worker pool.
      net::post(httpPool_, [weakSelf, botToken,
                            reply = std::move(reply)]() mutable {
        try {
          json::value res =
              httpsPostJson("slack.com", "/api/chat.postMessage", botToken,
                            json::value(std::move(reply)));

          if (auto self2 = weakSelf.lock()) {
            net::post(self2->strand_, [self2, res = std::move(res)]() mutable {
              if (!res.is_object() || !getBool(res.as_object(), "ok")) {
                log("chat.postMessage failed: " + json::serialize(res));
              }
            });
          }
        } catch (const std::exception &e) {
          if (auto self2 = weakSelf.lock()) {
            const std::string errorText = e.what();
            net::post(self2->strand_, [self2, errorText] {
              (void)self2;
              log("chat.postMessage error: " + errorText);
            });
          }
        }
      });
    } else {
      auto aiClient = std::make_shared<AiAgentClient>(aiIoc_, sslCtx_,
                                                      aiEndpoint_, aiToken_);

      if (APP_DEBUG_AI) {
        log("doing ai...");
      }
      aiClient->query(prompt, [weakSelf, aiClient, botToken, replyChannel,
                               replyThread,
                               replyOnThread](std::exception_ptr error,
                                              std::string aiReply) mutable {
        if (auto self = weakSelf.lock()) {
          net::post(self->strand_, [self, weakSelf, botToken, replyChannel,
                                    replyThread, replyOnThread,
                                    error = std::move(error),
                                    aiReply = std::move(aiReply)]() mutable {
            if (APP_DEBUG_AI) {
              log("ai responded...");
            }
            if (error) {
              try {
                std::rethrow_exception(error);
              } catch (const std::exception &e) {
                log(std::string("AI agent query failed: ") + e.what());
                json::object message;
                message["channel"] = replyChannel;
                message["text"] = std::string("⚠️ AI error: ") + e.what();

                net::post(self->httpPool_, [weakSelf, botToken,
                                            reply =
                                                std::move(message)]() mutable {
                  try {
                    json::value res =
                        httpsPostJson("slack.com", "/api/chat.postMessage",
                                      botToken, json::value(std::move(reply)));

                    if (auto self2 = weakSelf.lock()) {
                      net::post(self2->strand_,
                                [self2, res = std::move(res)]() mutable {
                                  if (!res.is_object() ||
                                      !getBool(res.as_object(), "ok")) {
                                    log("chat.postMessage failed: " +
                                        json::serialize(res));
                                  }
                                });
                    }
                  } catch (const std::exception &e) {
                    if (auto self2 = weakSelf.lock()) {
                      const std::string errorText = e.what();
                      net::post(self2->strand_, [self2, errorText] {
                        (void)self2;
                        log("chat.postMessage error: " + errorText);
                      });
                    }
                  }
                });
              }
              return;
            }

            if (aiReply.empty()) {
              log("AI agent returned an empty reply.");
              return;
            }

            json::object message;
            message["channel"] = replyChannel;
            message["text"] = "AI response";

            json::array blocks;

            json::object headerText;
            headerText["type"] = "plain_text";
            headerText["text"] = "Here is your answer";
            headerText["emoji"] = true;

            json::object header;
            header["type"] = "header";
            header["text"] = std::move(headerText);
            blocks.push_back(std::move(header));

            json::object sectionText;
            sectionText["type"] = "mrkdwn";
            sectionText["text"] = aiReply;

            json::object section;
            section["type"] = "section";
            section["text"] = std::move(sectionText);
            blocks.push_back(std::move(section));

            json::object divider;
            divider["type"] = "divider";
            blocks.push_back(std::move(divider));

            message["blocks"] = std::move(blocks);

            if (replyOnThread && !replyThread.empty()) {
              message["thread_ts"] = replyThread;
            }

            // Slack chat.postMessage is still blocking, so keep it off
            // the WebSocket strand on the existing HTTP worker pool.
            net::post(self->httpPool_, [weakSelf, botToken,
                                        reply = std::move(message)]() mutable {
              try {
                json::value res =
                    httpsPostJson("slack.com", "/api/chat.postMessage",
                                  botToken, json::value(std::move(reply)));

                if (auto self2 = weakSelf.lock()) {
                  net::post(self2->strand_, [self2,
                                             res = std::move(res)]() mutable {
                    if (!res.is_object() || !getBool(res.as_object(), "ok")) {
                      log("chat.postMessage failed: " + json::serialize(res));
                    }
                  });
                }
              } catch (const std::exception &e) {
                if (auto self2 = weakSelf.lock()) {
                  const std::string errorText = e.what();
                  net::post(self2->strand_, [self2, errorText] {
                    (void)self2;
                    log("chat.postMessage error: " + errorText);
                  });
                }
              }
            });
          });
        }
      });
    }
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
  const char *aiTokenEnv = std::getenv("SLACK_AI_AGENT_TOKEN");

  if (!botTokenEnv || !appTokenEnv || !aiTokenEnv || !*aiTokenEnv) {
    std::cerr << "Set SLACK_BOT_TOKEN, SLACK_APP_TOKEN, and "
                 "SLACK_AI_AGENT_TOKEN environment variables."
              << std::endl;
    return 1;
  }

  const std::string botToken = botTokenEnv;
  const std::string appToken = appTokenEnv;
  const std::string aiToken = aiTokenEnv;

  // OpenAI-compatible Chat Completions endpoint. Override this for a
  // self-hosted/proxy endpoint while keeping the same API contract.
  const char *aiEndpointEnv = std::getenv("SLACK_AI_AGENT_ENDPOINT");
  const std::string aiEndpoint =
      (aiEndpointEnv && *aiEndpointEnv)
          ? aiEndpointEnv
          : "https://api.openai.com/v1/chat/completions";

  net::io_context ioc;
  net::io_context aiIoc;
  net::thread_pool httpPool(4);

  ssl::context ctx{ssl::context::tlsv12_client};
  ctx.set_default_verify_paths();
  ctx.set_verify_mode(ssl::verify_peer);

  std::cout << "ArphaXAD is running in socket mode..." << std::endl;
  std::cout << "AI agent endpoint: " << aiEndpoint << std::endl;

  // The AI agent has its own Asio io_context so its DNS/TLS/HTTP work is
  // isolated from the Slack WebSocket event loop.
  // Keep it alive even when there is temporarily no queued work; otherwise
  // io_context::run() returns immediately and later query() calls never run.
  auto aiWorkGuard = net::make_work_guard(aiIoc);
  std::thread aiIoThread([&aiIoc] { aiIoc.run(); });

  while (true) {
    try {
      // This is done outside the WebSocket strand. It is a short,
      // one-shot connection used only to obtain the Socket Mode URL.
      const std::string wsUrl = openSocketModeUrl(appToken);

      auto session = std::make_shared<SocketModeSession>(
          ioc, ctx, botToken, wsUrl, httpPool, aiIoc, aiEndpoint, aiToken);

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

  aiIoc.stop();
  if (aiIoThread.joinable()) {
    aiIoThread.join();
  }
  httpPool.join();
}

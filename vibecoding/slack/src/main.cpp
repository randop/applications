// slack_bot.cpp
//
// Slack Socket Mode bot rewritten in the production style of
// https_client.cpp / websocket_client.cpp:
//   • strand-serialized stackful coroutines (boost::asio::spawn)
//   • sequential yield[ec] control flow
//   • exceptions → boost::system::system_error (single try/catch)
//   • one shared HTTPS client + one WebSocket session coroutine
//
// Build (Ubuntu 26.04 / Boost ≥ 1.92):
//   g++ -std=c++17 -O2 -pthread slack_socket_mode.cpp -o slack_socket_mode \
//       -lboost_context -lboost_json -lssl -lcrypto
//
// Run:
//   export SLACK_BOT_TOKEN=xoxb-...
//   export SLACK_APP_TOKEN=xapp-...
//   export SLACK_AI_AGENT_TOKEN=...
//   export SLACK_AI_MODEL=meta/muse-glimmer-30b          # optional
//   export SLACK_AI_AGENT_ENDPOINT=https://...           # optional
//   export SLACK_BOOT_CHANNEL=C...                       # optional
//   ./slack_socket_mode

#define APP_VERSION "1.0.6"

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
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
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace json = boost::json;

using tcp = asio::ip::tcp;
using error_code = boost::system::error_code;

// ---------------------------------------------------------------------------
// Configuration / helpers (unchanged semantics)
// ---------------------------------------------------------------------------

constexpr bool APP_DEBUG_MESSAGES = false;
constexpr bool APP_DEBUG_AI = false;

static std::string g_AI_model = "meta/muse-glimmer-30b";

constexpr std::size_t kMaxSectionTextBytes = 3000;

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

std::string removeSlackMentions(const std::string &text) {
  std::string result = std::regex_replace(text, std::regex(R"(<@[^>]+>)"), "");
  result.erase(0, result.find_first_not_of(" \t\r\n"));
  result.erase(result.find_last_not_of(" \t\r\n") + 1);
  return result;
}

std::string removeOnString(const std::string &text,
                           const std::string &expression) {
  std::string result = std::regex_replace(text, std::regex(expression), "");
  result.erase(0, result.find_first_not_of(" \t\r\n"));
  result.erase(result.find_last_not_of(" \t\r\n") + 1);
  return result;
}

bool starts_with(const std::string &str, const std::string &prefix) {
  return str.size() >= prefix.size() &&
         str.compare(0, prefix.size(), prefix) == 0;
}

// ---------------------------------------------------------------------------
// UTF-8 / Slack mrkdwn helpers (identical to original)
// ---------------------------------------------------------------------------

std::size_t utf8CodePointLength(std::string_view s, std::size_t pos) {
  const auto c = static_cast<unsigned char>(s[pos]);
  if (c < 0x80) {
    return 1;
  }
  if ((c & 0xE0) == 0xC0) {
    return 2;
  }
  if ((c & 0xF0) == 0xE0) {
    return 3;
  }
  if ((c & 0xF8) == 0xF0) {
    return 4;
  }
  return 0;
}

bool isValidUtf8(std::string_view s) {
  for (std::size_t i = 0; i < s.size();) {
    const auto len = utf8CodePointLength(s, i);
    if (len == 0 || i + len > s.size()) {
      return false;
    }
    const auto c0 = static_cast<unsigned char>(s[i]);
    for (std::size_t j = 1; j < len; ++j) {
      const auto c = static_cast<unsigned char>(s[i + j]);
      if ((c & 0xC0) != 0x80) {
        return false;
      }
    }
    if (len == 2 && c0 < 0xC2) {
      return false;
    }
    if (len == 3) {
      const auto c1 = static_cast<unsigned char>(s[i + 1]);
      if (c0 == 0xE0 && c1 < 0xA0) {
        return false;
      }
      if (c0 == 0xED && c1 >= 0xA0) {
        return false;
      }
    }
    if (len == 4) {
      const auto c1 = static_cast<unsigned char>(s[i + 1]);
      if (c0 == 0xF0 && c1 < 0x90) {
        return false;
      }
      if (c0 == 0xF4 && c1 >= 0x90) {
        return false;
      }
      if (c0 > 0xF4) {
        return false;
      }
    }
    i += len;
  }
  return true;
}

struct MarkdownState {
  bool inlineCode = false;
  bool fencedCode = false;
};

bool isCodeFence(std::string_view text, std::size_t pos) {
  return pos + 3 <= text.size() && text.compare(pos, 3, "```") == 0;
}

std::size_t findMarkdownSplitPoint(std::string_view text,
                                   std::size_t maxBytes) {
  if (text.size() <= maxBytes) {
    return text.size();
  }

  MarkdownState state;
  std::size_t lastSafe = 0, lastWhitespace = 0, lastLineBreak = 0;

  for (std::size_t i = 0; i < text.size();) {
    const auto len = utf8CodePointLength(text, i);
    if (len == 0 || i + len > text.size()) {
      break;
    }

    if (!state.inlineCode && isCodeFence(text, i)) {
      state.fencedCode = !state.fencedCode;
      i += 3;
      continue;
    }
    if (!state.fencedCode && text[i] == '`') {
      state.inlineCode = !state.inlineCode;
      i += 1;
      continue;
    }
    if (!state.inlineCode && !state.fencedCode) {
      if (text[i] == '\n') {
        lastLineBreak = i + 1;
        lastSafe = i + 1;
      } else if (text[i] == ' ' || text[i] == '\t') {
        lastWhitespace = i + 1;
        lastSafe = i + 1;
      }
    }
    if (i + len > maxBytes) {
      break;
    }
    i += len;
  }

  if (lastLineBreak > 0 && lastLineBreak <= maxBytes) {
    return lastLineBreak;
  }
  if (lastWhitespace > 0 && lastWhitespace <= maxBytes) {
    return lastWhitespace;
  }

  std::size_t safe = 0;
  for (std::size_t i = 0; i < text.size();) {
    const auto len = utf8CodePointLength(text, i);
    if (len == 0 || i + len > maxBytes) {
      break;
    }
    safe = i + len;
    i += len;
  }
  if (safe == 0) {
    throw std::runtime_error("Unable to find UTF-8-safe Slack split point");
  }
  return safe;
}

std::vector<std::string> splitSlackMrkdwn(std::string_view text) {
  std::vector<std::string> result;
  if (text.empty()) {
    result.emplace_back();
    return result;
  }
  result.reserve(text.size() / kMaxSectionTextBytes + 1);

  while (!text.empty()) {
    const auto split = findMarkdownSplitPoint(text, kMaxSectionTextBytes);
    if (split == 0) {
      throw std::runtime_error("Slack mrkdwn split produced an empty chunk");
    }
    result.emplace_back(text.substr(0, split));
    text.remove_prefix(split);
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
      text.remove_prefix(1);
    }
  }
  return result;
}

json::object normalizeBlocksForSlack(const json::object &payload) {
  json::object output = payload;
  const auto blocksIt = payload.if_contains("blocks");
  if (!blocksIt) {
    return output;
  }
  if (!blocksIt->is_array()) {
    throw std::invalid_argument("'blocks' must be an array");
  }

  const auto &blocks = blocksIt->as_array();
  json::array normalizedBlocks;
  normalizedBlocks.reserve(blocks.size());

  for (const auto &blockValue : blocks) {
    if (!blockValue.is_object()) {
      normalizedBlocks.push_back(blockValue);
      continue;
    }
    const auto &block = blockValue.as_object();
    const auto typeIt = block.if_contains("type");
    if (!typeIt || !typeIt->is_string() || typeIt->as_string() != "section") {
      normalizedBlocks.push_back(blockValue);
      continue;
    }
    const auto textIt = block.if_contains("text");
    if (!textIt || !textIt->is_object()) {
      normalizedBlocks.push_back(blockValue);
      continue;
    }
    const auto &textObject = textIt->as_object();
    const auto textTypeIt = textObject.if_contains("type");
    const auto textValueIt = textObject.if_contains("text");
    if (!textTypeIt || !textTypeIt->is_string() || !textValueIt ||
        !textValueIt->is_string()) {
      normalizedBlocks.push_back(blockValue);
      continue;
    }

    const auto textType = textTypeIt->as_string();
    const auto &textValue = textValueIt->as_string();
    const std::string_view text(textValue.data(), textValue.size());

    if (!isValidUtf8(text)) {
      throw std::invalid_argument("Slack section text contains invalid UTF-8");
    }

    if (text.size() <= kMaxSectionTextBytes) {
      normalizedBlocks.push_back(blockValue);
      continue;
    }

    std::vector<std::string> chunks;
    if (textType == "mrkdwn" || textType == "plain_text") {
      chunks = splitSlackMrkdwn(text);
    } else {
      throw std::invalid_argument("Unsupported Slack text type");
    }

    for (auto &chunk : chunks) {
      json::object newText = textObject;
      newText["text"] = std::move(chunk);
      json::object newBlock = block;
      newBlock["text"] = std::move(newText);
      normalizedBlocks.push_back(std::move(newBlock));
    }
  }
  output["blocks"] = std::move(normalizedBlocks);
  return output;
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

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

std::string extractAiAgentReply(const json::value &res) {
  if (!res.is_object()) {
    throw std::runtime_error("AI agent returned a non-object JSON response.");
  }

  const auto &obj = res.as_object();

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
  if (auto *output = obj.if_contains("output"); output && output->is_string()) {
    return std::string(output->as_string());
  }

  throw std::runtime_error(
      "AI agent response did not contain an assistant message content.");
}

// ---------------------------------------------------------------------------
// HTTPS client  (identical design to the provided https_client.cpp)
// ---------------------------------------------------------------------------

enum class http_method {
  GET,
  POST,
  PUT,
  DELETE_,
  PATCH,
  HEAD,
  OPTIONS,
  TRACE,
  CONNECT
};

inline std::string_view to_string(http_method m) {
  switch (m) {
  case http_method::GET:
    return "GET";
  case http_method::POST:
    return "POST";
  case http_method::PUT:
    return "PUT";
  case http_method::DELETE_:
    return "DELETE";
  case http_method::PATCH:
    return "PATCH";
  case http_method::HEAD:
    return "HEAD";
  case http_method::OPTIONS:
    return "OPTIONS";
  case http_method::TRACE:
    return "TRACE";
  case http_method::CONNECT:
    return "CONNECT";
  }
  return "GET";
}

struct http_request {
  http_method method = http_method::GET;
  std::string host;
  std::string port = "443";
  std::string target = "/";
  std::map<std::string, std::string> headers;
  std::string body;
};

struct http_response {
  int status_code = 0;
  std::string status_message;
  std::map<std::string, std::string> headers;
  std::string body;
};

class https_client {
public:
  explicit https_client(asio::io_context &httpIoc, ssl::context &httpSslCtx)
      : ioc_(httpIoc), strand_(asio::make_strand(httpIoc)), ssl_ctx_(httpSslCtx)

  {
    ssl_ctx_.set_default_verify_paths();
    ssl_ctx_.set_verify_mode(ssl::verify_peer);
  }

  // Generic coroutine entry point (throws on error)
  http_response async_request(http_request req, asio::yield_context yield) {
    return do_request(std::move(req), yield);
  }

  // Convenience: POST JSON
  http_response async_post_json(std::string_view host, std::string_view target,
                                const std::string &bearer,
                                const json::value &payload,
                                asio::yield_context yield) {
    http_request r;
    r.method = http_method::POST;
    r.host = std::string(host);
    r.target = std::string(target);
    r.body = json::serialize(payload);
    r.headers["Content-Type"] = "application/json; charset=utf-8";
    r.headers["Authorization"] = "Bearer " + bearer;
    r.headers["User-Agent"] = "boost-beast-slack-bot/1.1";
    return do_request(std::move(r), yield);
  }

  // Executor used by this client's I/O operations. A caller can spawn a
  // coroutine here so the network operation and its continuation stay off
  // the WebSocket event loop.
  auto executor() const { return strand_; }

private:
  http_response do_request(http_request req, asio::yield_context yield) {
    error_code ec;

    tcp::resolver resolver(strand_);
    auto endpoints = resolver.async_resolve(req.host, req.port, yield[ec]);
    if (ec) {
      throw boost::system::system_error(ec, "resolve");
    }

    ssl::stream<tcp::socket> stream(strand_, ssl_ctx_);

    if (!SSL_set_tlsext_host_name(stream.native_handle(), req.host.c_str())) {
      throw boost::system::system_error(static_cast<int>(::ERR_get_error()),
                                        asio::error::get_ssl_category(), "SNI");
    }

    asio::async_connect(stream.next_layer(), endpoints, yield[ec]);
    if (ec) {
      throw boost::system::system_error(ec, "connect");
    }

    stream.async_handshake(ssl::stream_base::client, yield[ec]);
    if (ec) {
      throw boost::system::system_error(ec, "handshake");
    }

    // IMPORTANT: use Beast's HTTP parser instead of reading until EOF.
    // Many HTTP/1.1 servers/proxies keep the connection alive even when the
    // response body is already complete. Reading until EOF therefore makes
    // a perfectly valid response appear to hang forever.
    http::request<http::string_body> request;
    request.version(11);
    request.method(to_http_verb(req.method));
    request.target(req.target);
    request.set(http::field::host, req.host);
    request.set(http::field::connection, "keep-alive");
    request.set(http::field::user_agent, "boost-beast-slack-bot/1.1");
    for (const auto &[k, v] : req.headers) {
      request.set(k, v);
    }
    request.body() = req.body;
    request.prepare_payload();

    log("HTTP " + std::string(http::to_string(request.method())) + " https://" +
        req.host + req.target);

    http::async_write(stream, request, yield[ec]);
    if (ec) {
      throw boost::system::system_error(ec, "write");
    }

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::async_read(stream, buffer, response, yield[ec]);
    if (ec) {
      throw boost::system::system_error(ec, "read HTTP response");
    }

    http_response out;
    out.status_code = response.result_int();
    out.status_message = std::string(response.reason());
    for (const auto &field : response.base()) {
      out.headers[std::string(field.name_string())] =
          std::string(field.value());
    }
    out.body = std::move(response.body());

    log("HTTP response " + std::to_string(out.status_code) + " (" +
        std::to_string(out.body.size()) + " bytes)");

    // Best-effort TLS shutdown. The HTTP response has already been fully
    // consumed, so this is not part of determining response completion.
    stream.async_shutdown(yield[ec]);

    return out;
  }

  static http::verb to_http_verb(http_method m) {
    switch (m) {
    case http_method::GET:
      return http::verb::get;
    case http_method::POST:
      return http::verb::post;
    case http_method::PUT:
      return http::verb::put;
    case http_method::DELETE_:
      return http::verb::delete_;
    case http_method::PATCH:
      return http::verb::patch;
    case http_method::HEAD:
      return http::verb::head;
    case http_method::OPTIONS:
      return http::verb::options;
    case http_method::TRACE:
      return http::verb::trace;
    case http_method::CONNECT:
      return http::verb::connect;
    }
    return http::verb::get;
  }

  static http_response parse_response(std::string raw) {
    http_response resp;
    auto pos = raw.find("\r\n");
    if (pos == std::string::npos) {
      throw boost::system::system_error(asio::error::fault, "bad status line");
    }

    std::string status_line = raw.substr(0, pos);
    raw.erase(0, pos + 2);

    auto sp1 = status_line.find(' ');
    auto sp2 = status_line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
      throw boost::system::system_error(asio::error::fault, "malformed status");
    }

    resp.status_code = std::stoi(status_line.substr(sp1 + 1, sp2 - sp1 - 1));
    resp.status_message = status_line.substr(sp2 + 1);

    while (true) {
      pos = raw.find("\r\n");
      if (pos == std::string::npos) {
        break;
      }
      std::string line = raw.substr(0, pos);
      raw.erase(0, pos + 2);
      if (line.empty()) {
        break;
      }

      auto colon = line.find(':');
      if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) {
          val.erase(0, 1);
        }
        resp.headers[std::move(key)] = std::move(val);
      }
    }
    resp.body = std::move(raw);
    return resp;
  }

  asio::io_context &ioc_;
  asio::strand<asio::io_context::executor_type> strand_;
  ssl::context &ssl_ctx_;
};

// ---------------------------------------------------------------------------
// WebSocket endpoint parser (from websocket_client.cpp)
// ---------------------------------------------------------------------------

struct websocket_endpoint {
  std::string scheme = "wss";
  std::string host;
  std::string port;
  std::string target = "/";

  bool secure() const { return scheme == "wss"; }

  std::string effective_port() const {
    if (!port.empty()) {
      return port;
    }
    return secure() ? "443" : "80";
  }

  static websocket_endpoint parse(std::string_view uri) {
    websocket_endpoint ep;
    const auto scheme_pos = uri.find("://");
    if (scheme_pos == std::string_view::npos) {
      throw std::invalid_argument("WebSocket URI must contain ://");
    }

    ep.scheme = std::string(uri.substr(0, scheme_pos));
    if (ep.scheme != "ws" && ep.scheme != "wss") {
      throw std::invalid_argument("WebSocket URI scheme must be ws or wss");
    }

    const auto rest = uri.substr(scheme_pos + 3);
    const auto path_pos = rest.find_first_of("/?");
    const std::string_view authority =
        path_pos == std::string_view::npos ? rest : rest.substr(0, path_pos);

    ep.target = path_pos == std::string_view::npos
                    ? "/"
                    : std::string(rest.substr(path_pos));

    if (authority.empty()) {
      throw std::invalid_argument("WebSocket URI host is empty");
    }

    if (authority.front() == '[') {
      const auto close = authority.find(']');
      if (close == std::string_view::npos) {
        throw std::invalid_argument("Malformed IPv6 WebSocket URI");
      }
      ep.host = std::string(authority.substr(1, close - 1));
      if (close + 1 < authority.size()) {
        if (authority[close + 1] != ':') {
          throw std::invalid_argument("Malformed WebSocket URI port");
        }
        ep.port = std::string(authority.substr(close + 2));
      }
    } else {
      const auto colon = authority.rfind(':');
      if (colon != std::string_view::npos && authority.find(':') == colon) {
        ep.host = std::string(authority.substr(0, colon));
        ep.port = std::string(authority.substr(colon + 1));
      } else {
        ep.host = std::string(authority);
      }
    }
    if (ep.host.empty()) {
      throw std::invalid_argument("WebSocket URI host is empty");
    }
    for (char c : ep.port) {
      if (c < '0' || c > '9') {
        throw std::invalid_argument("WebSocket URI port must be numeric");
      }
    }
    return ep;
  }
};

// ---------------------------------------------------------------------------
// Socket Mode session – pure coroutine style
// ---------------------------------------------------------------------------

class SocketModeSession
    : public std::enable_shared_from_this<SocketModeSession> {
  using strand_type = asio::strand<asio::io_context::executor_type>;
  using websocket_type = websocket::stream<ssl::stream<tcp::socket>>;

public:
  SocketModeSession(asio::io_context &wsIoc, ssl::context &wsSslCtx,
                    https_client &http, https_client &aiHttp,
                    std::string botToken, std::string appToken,
                    std::string aiEndpoint, std::string aiToken)
      : ioc_(wsIoc), strand_(asio::make_strand(wsIoc)), sslCtx_(wsSslCtx),
        http_(http), aiHttp_(aiHttp), botToken_(std::move(botToken)),
        appToken_(std::move(appToken)), aiEndpoint_(std::move(aiEndpoint)),
        aiToken_(std::move(aiToken)), pingTimer_(strand_) {}

  // Public entry – launches the long-lived session coroutine
  void start() {
    asio::spawn(
        strand_,
        [self = shared_from_this()](asio::yield_context yield) {
          self->run(yield);
        },
        asio::detached);
  }

private:
  asio::io_context &ioc_;
  strand_type strand_;
  https_client &http_;   // Slack HTTP client
  https_client &aiHttp_; // AI-agent HTTP client
  ssl::context &sslCtx_;
  std::string botToken_;
  std::string appToken_;
  std::string aiEndpoint_;
  std::string aiToken_;

  std::unique_ptr<websocket_type> ws_;
  beast::flat_buffer readBuffer_;
  asio::steady_timer pingTimer_;
  std::chrono::steady_clock::time_point lastPong_;
  std::chrono::steady_clock::time_point lastPingAt_;
  bool stopped_ = false;
  // Accessed only from the WebSocket strand. The AI worker never reads or
  // writes this directly; completion is posted back to the strand first.
  bool aiBusy_ = false;

  static constexpr auto kPingInterval = std::chrono::seconds(20);
  static constexpr auto kPongTimeout =
      std::chrono::seconds(45); // more tolerant of 5G LTE

  // -----------------------------------------------------------------
  // Top-level session coroutine (the elegant linear flow)
  // -----------------------------------------------------------------
  void run(asio::yield_context yield) {
    for (;;) {
      try {
        do_one_session(yield);
      } catch (const boost::system::system_error &e) {
        log(std::string("Session ended: ") + e.what());
      } catch (const std::exception &e) {
        log(std::string("Session ended: ") + e.what());
      }

      // Always reconnect unless we are shutting down the whole process
      log("Reconnecting in 2s with flaky network protection...");
      asio::steady_timer delay(strand_);
      delay.expires_after(std::chrono::seconds(2));
      error_code ec;
      delay.async_wait(yield[ec]);
    }
  }

  void do_one_session(asio::yield_context yield) {
    // 1. Obtain a fresh Socket Mode URL (HTTPS coroutine)
    const std::string wsUrl = open_socket_mode_url(yield);
    log("Got Socket Mode URL");

    // 2. Parse & connect WebSocket
    auto ep = websocket_endpoint::parse(wsUrl);
    connect_websocket(ep, yield);
    log("WebSocket handshake complete.");

    // 3. Boot notification (fire-and-forget coroutine)
    send_boot_notification(yield);

    // 4. Start ping timer + enter the read loop
    lastPong_ = std::chrono::steady_clock::now();
    lastPingAt_ = lastPong_;
    schedule_ping(yield);

    // 5. Read loop – sequential, exception-driven
    for (;;) {
      auto msg = receive_message(yield);
      handle_envelope(msg, yield);
    }
  }

  // -----------------------------------------------------------------
  // HTTPS helpers (Slack HTTP uses the dedicated Slack client)
  // -----------------------------------------------------------------
  std::string open_socket_mode_url(asio::yield_context yield) {
    auto resp = http_.async_post_json("slack.com", "/api/apps.connections.open",
                                      appToken_, json::object{}, yield);

    if (resp.status_code < 200 || resp.status_code >= 300) {
      throw std::runtime_error("apps.connections.open HTTP " +
                               std::to_string(resp.status_code));
    }

    auto j = json::parse(resp.body);
    if (!j.is_object() || !getBool(j.as_object(), "ok")) {
      throw std::runtime_error("apps.connections.open failed: " + resp.body);
    }
    return getStr(j.as_object(), "url");
  }

  void post_message(const json::object &message, asio::yield_context yield) {
    auto resp = http_.async_post_json("slack.com", "/api/chat.postMessage",
                                      botToken_, message, yield);

    if (resp.status_code < 200 || resp.status_code >= 300) {
      log("chat.postMessage HTTP " + std::to_string(resp.status_code) + ": " +
          resp.body);
      return;
    }
    auto j = json::parse(resp.body);
    if (!j.is_object() || !getBool(j.as_object(), "ok")) {
      log("chat.postMessage failed: " + resp.body);
    }
  }

  // Never make the WebSocket reader wait for Slack's HTTP API.
  void post_message_async(json::object message) {
    auto self = shared_from_this();
    asio::spawn(
        http_.executor(),
        [self,
         message = std::move(message)](asio::yield_context yield) mutable {
          try {
            self->post_message(message, yield);
          } catch (const std::exception &e) {
            log(std::string("Slack post failed: ") + e.what());
          }
        },
        asio::detached);
  }

  void connect_websocket(const websocket_endpoint &ep,
                         asio::yield_context yield) {
    error_code ec;
    ws_.reset();

    tcp::resolver resolver(strand_);
    auto endpoints =
        resolver.async_resolve(ep.host, ep.effective_port(), yield[ec]);
    if (ec) {
      throw boost::system::system_error(ec, "resolve");
    }

    auto stream = std::make_unique<websocket_type>(strand_, sslCtx_);

    if (!SSL_set_tlsext_host_name(stream->next_layer().native_handle(),
                                  ep.host.c_str())) {
      throw boost::system::system_error(static_cast<int>(::ERR_get_error()),
                                        asio::error::get_ssl_category(), "SNI");
    }

    asio::async_connect(stream->next_layer().next_layer(), endpoints,
                        yield[ec]);
    if (ec) {
      throw boost::system::system_error(ec, "connect");
    }

    stream->next_layer().async_handshake(ssl::stream_base::client, yield[ec]);
    if (ec) {
      throw boost::system::system_error(ec, "TLS handshake");
    }

    stream->set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::client));
    stream->set_option(
        websocket::stream_base::decorator([](websocket::request_type &req) {
          req.set(http::field::user_agent, "boost-beast-slack-bot/1.1");
        }));

    stream->async_handshake(ep.host, ep.target, yield[ec]);
    if (ec) {
      throw boost::system::system_error(ec, "WebSocket handshake");
    }

    stream->control_callback(
        [self = shared_from_this()](websocket::frame_type kind,
                                    beast::string_view) {
          if (kind == websocket::frame_type::pong) {
            asio::post(self->strand_, [self] {
              self->lastPong_ = std::chrono::steady_clock::now();
            });
          }
        });

    ws_ = std::move(stream);
  }

  // -----------------------------------------------------------------
  // Receive (make every error fatal for the session)
  // -----------------------------------------------------------------
  std::string receive_message(asio::yield_context yield) {
    if (!ws_ || !ws_->is_open()) {
      throw boost::system::system_error(asio::error::not_connected,
                                        "WebSocket not open");
    }

    error_code ec;
    readBuffer_.clear();
    ws_->async_read(readBuffer_, yield[ec]);

    // Any error means the session is dead → let the outer loop reconnect
    if (ec) {
      if (ec == websocket::error::closed) {
        log("WebSocket closed by peer.");
      } else {
        log("WebSocket read error: " + ec.message());
      }
      throw boost::system::system_error(ec, "WebSocket read");
    }

    return beast::buffers_to_string(readBuffer_.data());
  }

  void send_text(const std::string &data, asio::yield_context yield) {
    if (!ws_) {
      throw boost::system::system_error(asio::error::not_connected,
                                        "WebSocket not open");
    }

    error_code ec;
    ws_->text(true);
    ws_->async_write(asio::buffer(data), yield[ec]);
    if (ec) {
      throw boost::system::system_error(ec, "WebSocket write");
    }
  }

  // -----------------------------------------------------------------
  // Envelope handling (still sequential)
  // -----------------------------------------------------------------
  void handle_envelope(const std::string &raw, asio::yield_context yield) {
    json::value envelope;
    try {
      envelope = json::parse(raw);
    } catch (const std::exception &e) {
      log(std::string("Failed to parse envelope: ") + e.what());
      return;
    }
    if (!envelope.is_object()) {
      return;
    }

    const auto &obj = envelope.as_object();
    const std::string type = getStr(obj, "type");

    if (type == "hello") {
      log("Socket Mode connection established (hello).");
      return;
    }
    if (type == "disconnect") {
      log("Received disconnect (reason: " + getStr(obj, "reason", "unknown") +
          "); reconnecting...");
      throw boost::system::system_error(asio::error::connection_reset,
                                        "disconnect");
    }

    // ACK
    if (has(obj, "envelope_id")) {
      json::object ack;
      ack["envelope_id"] = getStr(obj, "envelope_id");
      send_text(json::serialize(ack), yield);
    }

    if (type != "events_api") {
      return;
    }

    const json::object &payload = getObj(obj, "payload");
    const json::object &event = getObj(payload, "event");
    const std::string eventType = getStr(event, "type");

    if (eventType == "message" || eventType == "app_mention") {
      handle_message_event(event, yield);
    }
  }

  void handle_message_event(const json::object &event,
                            asio::yield_context /*yield*/) {
    if (has(event, "subtype") || has(event, "bot_id")) {
      return;
    }

    const std::string eventType = getStr(event, "type");
    if (eventType != "app_mention" && getStr(event, "channel_type") != "im") {
      return;
    }

    const std::string text = getStr(event, "text");
    const std::string channel = getStr(event, "channel");
    const std::string clientMsgId = getStr(event, "client_msg_id");

    if (text.empty() || channel.empty()) {
      return;
    }

    log("Received message: " + clientMsgId);
    if (APP_DEBUG_MESSAGES) {
      log(json::serialize(event));
    }

    const std::string prompt = removeSlackMentions(text);

    // ---- built-in commands (no AI) ----
    bool isBotTask = false;
    json::object reply;

    if (is_equals(prompt, "ping")) {
      reply["text"] = "*pong*";
      isBotTask = true;
    } else if (is_equals(prompt, "/version")) {
      reply["text"] = APP_VERSION;
      isBotTask = true;
    } else if (starts_with(prompt, "/use ")) {
      std::string model = removeOnString(prompt, "/use ");
      std::string prev = g_AI_model;
      g_AI_model = model;
      reply["text"] = "switching from `" + prev + "` to `" + g_AI_model +
                      "` AI model. **Ready** 👍";
      isBotTask = true;
    } else if (is_equals(prompt, "/model")) {
      reply["text"] = g_AI_model;
      isBotTask = true;
    }

    if (isBotTask) {
      reply["channel"] = channel;
      post_message_async(std::move(reply));
      return;
    }

    // ---- AI path ----
    //
    // Message processing is deliberately independent of the Socket Mode
    // connection. We ACK the event first, then either reject it immediately
    // when another AI request is active or dispatch exactly one AI worker.
    //
    // aiBusy_ is protected by the WebSocket strand. The AI worker never
    // touches it directly; completion is posted back to this strand.
    if (aiBusy_) {
      json::object busyReply;
      busyReply["channel"] = channel;
      busyReply["text"] = "⏳ I'm currently thinking about another request. "
                          "Please try again when that response is ready.";
      post_message_async(std::move(busyReply));
      log("AI busy; rejected message " + clientMsgId);
      return;
    }

    aiBusy_ = true;

    // Snapshot the model while on the strand. The worker then owns its copy
    // and does not race with /use commands handled by the WebSocket strand.
    const std::string model = g_AI_model;
    auto self = shared_from_this();

    asio::spawn(
        aiHttp_.executor(),
        [self, prompt, channel, model](asio::yield_context aiYield) {
          try {
            log("doing ai stuff...");
            const std::string aiReply = self->query_ai(prompt, model, aiYield);

            // The AI job is independent of Socket Mode. Even if the
            // WebSocket disconnected/reconnected while the model was
            // thinking, the result must still be delivered through Slack's
            // HTTP API.
            asio::post(self->strand_, [self, channel, aiReply] {
              self->aiBusy_ = false;

              if (aiReply.empty()) {
                log("AI agent returned empty reply");
                return;
              }

              json::object message;
              message["channel"] = channel;
              message["text"] = "AI response";

              json::array blocks;
              {
                json::object headerText;
                headerText["type"] = "plain_text";
                headerText["text"] = "Here is your answer";
                headerText["emoji"] = true;

                json::object header;
                header["type"] = "header";
                header["text"] = std::move(headerText);
                blocks.push_back(std::move(header));
              }
              {
                json::object sectionText;
                sectionText["type"] = "mrkdwn";
                sectionText["text"] = aiReply;

                json::object section;
                section["type"] = "section";
                section["text"] = std::move(sectionText);
                blocks.push_back(std::move(section));
              }
              {
                json::object divider;
                divider["type"] = "divider";
                blocks.push_back(std::move(divider));
              }

              message["blocks"] = std::move(blocks);
              auto normalized = normalizeBlocksForSlack(message);
              self->post_message_async(std::move(normalized));
            });
          } catch (const std::exception &e) {
            const std::string error = e.what();
            log(std::string("AI agent query failed: ") + error);

            // Reset busy state and report the error through Slack HTTP even
            // when the Socket Mode WebSocket has disappeared.
            asio::post(self->strand_, [self, channel, error] {
              self->aiBusy_ = false;

              json::object errMsg;
              errMsg["channel"] = channel;
              errMsg["text"] = std::string("⚠️ AI error: ") + error;
              self->post_message_async(std::move(errMsg));
            });
          }
        },
        asio::detached);
  }

  // -----------------------------------------------------------------
  // AI agent query (dedicated HTTPS coroutine + OpenAI-compatible body)
  // -----------------------------------------------------------------
  std::string query_ai(const std::string &prompt, const std::string &model,
                       asio::yield_context yield) {
    // System prompt is the long Slack-mrkdwn instruction from the original.
    // (kept abbreviated here for readability – paste the full text from
    //  the original AiAgentClient if you need every rule)
    static const std::string systemPrompt =
        "You are responding in Slack.\n"
        "Your output MUST use Slack `mrkdwn` formatting...\n"
        "(full original system prompt)";

    json::object systemMessage;
    systemMessage["role"] = "system";
    systemMessage["content"] = systemPrompt;

    json::object userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = prompt;

    json::array messages;
    messages.push_back(std::move(systemMessage));
    messages.push_back(std::move(userMessage));

    json::object body;
    body["model"] = model;
    body["messages"] = std::move(messages);
    body["stream"] = false;
    body["max_tokens"] = 8192;
    body["temperature"] = 1;

    // Parse the AI endpoint once
    static const std::string prefix = "https://";
    if (aiEndpoint_.rfind(prefix, 0) != 0) {
      throw std::runtime_error("AI endpoint must be https://");
    }
    const std::string rest = aiEndpoint_.substr(prefix.size());
    const auto slash = rest.find('/');
    const std::string host =
        (slash == std::string::npos) ? rest : rest.substr(0, slash);
    const std::string target =
        (slash == std::string::npos) ? "/" : rest.substr(slash);

    log("AI HTTP request starting: https://" + host + target);
    auto resp = aiHttp_.async_post_json(host, target, aiToken_, body, yield);
    log("AI HTTP request completed with status " +
        std::to_string(resp.status_code));

    if (resp.status_code < 200 || resp.status_code >= 300) {
      throw std::runtime_error("AI agent HTTP " +
                               std::to_string(resp.status_code) + ": " +
                               resp.body);
    }

    return extractAiAgentReply(json::parse(resp.body));
  }

  // -----------------------------------------------------------------
  // Boot notification
  // -----------------------------------------------------------------
  void send_boot_notification(asio::yield_context yield) {
    const char *channelEnv = std::getenv("SLACK_BOOT_CHANNEL");
    if (!channelEnv || !*channelEnv) {
      log("SLACK_BOOT_CHANNEL not set; skipping boot notification.");
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

    json::array blocks;
    {
      json::object headerText;
      headerText["type"] = "plain_text";
      headerText["text"] = "Boot Notification";
      headerText["emoji"] = true;
      json::object header;
      header["type"] = "header";
      header["text"] = std::move(headerText);
      blocks.push_back(std::move(header));
    }
    {
      json::object sectionText;
      sectionText["type"] = "mrkdwn";
      sectionText["text"] = "🚀 Ready";
      json::object section;
      section["type"] = "section";
      section["text"] = std::move(sectionText);

      json::array fields;
      {
        json::object f;
        f["type"] = "mrkdwn";
        f["text"] = "ℹ️ Boot ID: `" + bootId + "`";
        fields.push_back(std::move(f));
      }
      {
        json::object f;
        f["type"] = "mrkdwn";
        f["text"] = "ℹ️ Version: `" APP_VERSION "`";
        fields.push_back(std::move(f));
      }
      {
        json::object f;
        f["type"] = "mrkdwn";
        f["text"] = "🖥️ System: `Linux`";
        fields.push_back(std::move(f));
      }
      section["fields"] = std::move(fields);
      blocks.push_back(std::move(section));
    }
    {
      json::object divider;
      divider["type"] = "divider";
      blocks.push_back(std::move(divider));
    }
    message["blocks"] = std::move(blocks);

    try {
      post_message_async(std::move(message));
    } catch (const std::exception &e) {
      log(std::string("Boot notification error: ") + e.what());
    }
  }

  // -----------------------------------------------------------------
  // Ping / keep-alive (still on the same strand)
  // -----------------------------------------------------------------
  void schedule_ping(asio::yield_context /*yield*/) {
    // Launch a separate long-lived ping coroutine
    asio::spawn(
        strand_,
        [self = shared_from_this()](asio::yield_context y) {
          self->ping_loop(y);
        },
        asio::detached);
  }

  // -----------------------------------------------------------------
  // Forceful teardown – call this from anywhere on the strand
  // -----------------------------------------------------------------
  void force_reconnect(const char *reason) {

    log(std::string(reason));
    bool willTearDown = true;

    if (willTearDown) {
      log(std::string(reason) + " → tearing down session");

      // Cancel everything so pending reads/writes fail immediately
      error_code ec;
      if (ws_) {
        beast::get_lowest_layer(*ws_).cancel(
            ec); // ← critical for flaky networks
        if (ws_->is_open()) {
          ws_->async_close(websocket::close_code::going_away,
                           [](error_code) {}); // fire-and-forget
        }
      }
      pingTimer_.cancel();
      // The main do_one_session() will exit via the error from async_read
    }
  }

  // -----------------------------------------------------------------
  // Ping loop enhanced
  // -----------------------------------------------------------------
  void ping_loop(asio::yield_context yield) {
    error_code ec;
    for (;;) {
      pingTimer_.expires_after(std::chrono::seconds(1));
      pingTimer_.async_wait(yield[ec]);
      if (ec == asio::error::operation_aborted || !ws_) {
        return;
      }

      const auto now = std::chrono::steady_clock::now();

      if (now - lastPong_ > kPongTimeout) {
        force_reconnect("No pong within timeout");
        return; // main session will die and outer loop will reconnect
      }

      if (now - lastPingAt_ >= kPingInterval) {
        lastPingAt_ = now;
        if (ws_ && ws_->is_open()) {
          ws_->async_ping({}, yield[ec]);
          if (ec) {
            force_reconnect(("WebSocket ping failed: " + ec.message()).c_str());
            return;
          }
        }
      }
    }
  }
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
  const char *botTokenEnv = std::getenv("SLACK_BOT_TOKEN");
  const char *appTokenEnv = std::getenv("SLACK_APP_TOKEN");
  const char *aiTokenEnv = std::getenv("SLACK_AI_AGENT_TOKEN");

  if (!botTokenEnv || !appTokenEnv || !aiTokenEnv || !*aiTokenEnv) {
    std::cerr << "Set SLACK_BOT_TOKEN, SLACK_APP_TOKEN and "
                 "SLACK_AI_AGENT_TOKEN environment variables.\n";
    return 1;
  }

  const std::string botToken = botTokenEnv;
  const std::string appToken = appTokenEnv;
  const std::string aiToken = aiTokenEnv;

  const char *aiEndpointEnv = std::getenv("SLACK_AI_AGENT_ENDPOINT");
  const std::string aiEndpoint =
      (aiEndpointEnv && *aiEndpointEnv)
          ? aiEndpointEnv
          : "https://api.openai.com/v1/chat/completions";

  if (const char *model = std::getenv("SLACK_AI_MODEL"); model && *model) {
    g_AI_model = model;
  }

  // Keep WebSocket and HTTP traffic isolated.  Each stack owns its own
  // io_context/strand and TLS context so configuration and event-loop load
  // cannot leak across the two transports.
  asio::io_context wsIoc;
  asio::io_context httpIoc;   // Slack HTTP
  asio::io_context aiHttpIoc; // AI-agent HTTP

  ssl::context wsSslCtx{ssl::context::tls_client};
  wsSslCtx.set_default_verify_paths();
  wsSslCtx.set_verify_mode(ssl::verify_peer);

  ssl::context httpSslCtx{ssl::context::tls_client};
  httpSslCtx.set_default_verify_paths();
  httpSslCtx.set_verify_mode(ssl::verify_peer);

  // Keep the AI agent on a completely separate TLS context and event loop.
  // This lets AI-agent TLS policy evolve independently from Slack traffic.
  ssl::context aiHttpSslCtx{ssl::context::tls_client};
  aiHttpSslCtx.set_default_verify_paths();
  aiHttpSslCtx.set_verify_mode(ssl::verify_peer);

  https_client http(httpIoc, httpSslCtx);
  https_client aiHttp(aiHttpIoc, aiHttpSslCtx);

  // Keep all contexts alive even when temporarily idle.
  auto wsWork = asio::make_work_guard(wsIoc);
  auto httpWork = asio::make_work_guard(httpIoc);
  auto aiHttpWork = asio::make_work_guard(aiHttpIoc);

  // Slack and AI HTTP requests each execute on their own event loop.
  std::thread httpThread([&httpIoc] { httpIoc.run(); });
  std::thread aiHttpThread([&aiHttpIoc] { aiHttpIoc.run(); });

  std::cout << "ArphaXAD is running in socket mode...\n";
  std::cout << "AI agent endpoint: " << aiEndpoint << "\n";
  std::cout << "AI model: " << g_AI_model << "\n";

  auto session = std::make_shared<SocketModeSession>(
      wsIoc, wsSslCtx, http, aiHttp, botToken, appToken, aiEndpoint, aiToken);
  session->start();

  wsIoc.run();

  // wsIoc.run() returns when the session has stopped.  Release the HTTP
  // work guard and wait for its event loop to drain before exiting.
  httpWork.reset();
  httpIoc.stop();
  if (httpThread.joinable()) {
    httpThread.join();
  }

  aiHttpWork.reset();
  aiHttpIoc.stop();
  if (aiHttpThread.joinable()) {
    aiHttpThread.join();
  }

  return 0;
}

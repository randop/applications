// slack_socket_mode.cpp
//
// Barebones Slack Socket Mode bot using Boost.Beast (WebSocket + HTTPS over
// TLS) and Boost.JSON. Everything comes from Boost + OpenSSL — no third
// party JSON library needed. Mirrors this @slack/bolt reference app:
//
//   - Connects in Socket Mode (no public HTTP endpoint needed).
//   - Listens for `message` events.
//   - Ignores bot messages / message subtypes (edits, deletes, etc).
//   - Only replies to direct messages (channel_type == "im").
//   - Replies with "Acknowledged: *<text>*" (or in-thread "You said: *<text>*"
//     if willReplyOnThread is flipped to true, matching the JS reference).
//
// Build (Ubuntu 24.04):
//   apt-get install libboost-system-dev libssl-dev
//   g++ -std=c++17 -O2 slack_socket_mode.cpp -o slack_socket_mode -lboost_system -lssl -lcrypto -lpthread
//
// Run:
//   export SLACK_BOT_TOKEN=xoxb-...
//   export SLACK_APP_TOKEN=xapp-...
//   ./slack_socket_mode
//
// Notes:
//   - Single translation unit. Boost.JSON is built header-only right here
//     (BOOST_JSON_NO_LIB + boost/json/src.hpp) so no libboost_json link
//     dependency is needed beyond the Boost headers already required by Beast.
//   - Each HTTPS call opens a fresh TLS connection (simplicity over reuse).
//   - Reconnects automatically if Slack sends a "disconnect" envelope or the
//     socket drops.

#define BOOST_JSON_NO_LIB  // build Boost.JSON header-only from this one TU

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/json/src.hpp>  // header-only Boost.JSON implementation

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace json = boost::json;
using tcp = net::ip::tcp;

namespace {

// ---------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------

void log(const std::string& msg) { std::cerr << "[slack] " << msg << std::endl; }

const json::object kEmptyObject;

// Non-throwing boost::json::object accessors, with defaults for missing
// or wrong-typed keys — Slack payloads are untrusted input.
std::string getStr(const json::object& obj, std::string_view key, std::string def = "") {
    if (auto* p = obj.if_contains(key); p && p->is_string()) {
        return std::string(p->as_string());
    }
    return def;
}

bool getBool(const json::object& obj, std::string_view key, bool def = false) {
    if (auto* p = obj.if_contains(key); p && p->is_bool()) {
        return p->as_bool();
    }
    return def;
}

bool has(const json::object& obj, std::string_view key) { return obj.if_contains(key) != nullptr; }

const json::object& getObj(const json::object& obj, std::string_view key) {
    if (auto* p = obj.if_contains(key); p && p->is_object()) {
        return p->as_object();
    }
    return kEmptyObject;
}

// Perform a single HTTPS POST with a JSON body against Slack's API and
// return the parsed JSON response. Opens/tears down its own connection.
json::value httpsPostJson(net::io_context& ioc, ssl::context& ctx, const std::string& host,
                           const std::string& target, const std::string& bearerToken,
                           const json::value& body) {
    tcp::resolver resolver{ioc};
    auto const results = resolver.resolve(host, "443");

    ssl::stream<tcp::socket> stream{ioc, ctx};
    if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
        throw beast::system_error(
            beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()));
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
    stream.shutdown(ec);  // peer may close abruptly; ignore shutdown errors

    return json::parse(res.body());
}

// Split a wss://host[:port]/path?query URL into its host and target parts.
// Slack's apps.connections.open always returns wss:// with an implicit
// port of 443, so we don't bother parsing an explicit port.
struct WssUrl {
    std::string host;
    std::string target;
};

WssUrl parseWssUrl(const std::string& url) {
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

// ---------------------------------------------------------------------
// Slack event handling
// ---------------------------------------------------------------------

// Mirrors the reference app: reply in the channel, optionally threaded.
void handleMessageEvent(net::io_context& ioc, ssl::context& ctx, const std::string& botToken,
                         const json::object& event) {
    // Ignore edits/deletes/joins/etc and anything posted by a bot.
    if (has(event, "subtype") || has(event, "bot_id")) {
        return;
    }

    // Only handle direct messages.
    if (getStr(event, "channel_type") != "im") {
        return;
    }

    const std::string text = getStr(event, "text");
    const std::string channel = getStr(event, "channel");
    const std::string ts = getStr(event, "ts");
    const std::string client_msg_id = getStr(event, "client_msg_id");

    log("Received message: " + client_msg_id);

    const bool willReplyOnThread = false;

    json::object reply;
    reply["channel"] = channel;
    if (willReplyOnThread) {
        reply["text"] = "You said: *" + text + "*";
        reply["thread_ts"] = ts;
    } else {
        reply["text"] = "Acknowledged: *" + text + "*";
    }

    try {
        json::value res =
            httpsPostJson(ioc, ctx, "slack.com", "/api/chat.postMessage", botToken, json::value(reply));
        if (!getBool(res.as_object(), "ok")) {
            log("chat.postMessage failed: " + json::serialize(res));
        }
    } catch (const std::exception& e) {
        log(std::string("chat.postMessage error: ") + e.what());
    }
}

// Handle one decoded Socket Mode envelope. Acks anything carrying an
// envelope_id (required by Slack for every dispatched event type).
//
// wsWriteMutex guards every frame written to `ws` — both this function's acks
// and the pinger thread's ping frames — since Beast's websocket::stream isn't
// safe for concurrent writes from multiple threads.
void handleEnvelope(net::io_context& ioc, ssl::context& ctx, const std::string& botToken,
                     websocket::stream<ssl::stream<tcp::socket>>& ws, std::mutex& wsWriteMutex,
                     const json::object& envelope, bool& shouldReconnect) {
    const std::string type = getStr(envelope, "type");

    if (type == "hello") {
        log("Socket Mode connection established (hello received).");
        return;
    }

    if (type == "disconnect") {
        log("Received disconnect envelope (reason: " + getStr(envelope, "reason", "unknown") +
            "); reconnecting...");
        shouldReconnect = true;
        return;
    }

    if (has(envelope, "envelope_id")) {
        json::object ack;
        ack["envelope_id"] = getStr(envelope, "envelope_id");
        std::lock_guard<std::mutex> lock(wsWriteMutex);
        ws.write(net::buffer(json::serialize(json::value(ack))));
    }

    if (type == "events_api") {
        const json::object& payload = getObj(envelope, "payload");
        const json::object& event = getObj(payload, "event");
        if (getStr(event, "type") == "message") {
            handleMessageEvent(ioc, ctx, botToken, event);
        }
    }
    // Other envelope types (slash_commands, interactive, etc.) are
    // acknowledged above but otherwise ignored in this barebones app.
}

// ---------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------

// Ask Slack for a fresh Socket Mode WebSocket URL.
std::string openSocketModeUrl(net::io_context& ioc, ssl::context& ctx, const std::string& appToken) {
    json::value res = httpsPostJson(ioc, ctx, "slack.com", "/api/apps.connections.open", appToken,
                                     json::value(json::object{}));
    const json::object& obj = res.as_object();
    if (!getBool(obj, "ok")) {
        throw std::runtime_error("apps.connections.open failed: " + json::serialize(res));
    }
    return getStr(obj, "url");
}

// Connect the Socket Mode WebSocket and run the read loop until Slack
// asks us to disconnect or the connection drops.
void runSocketModeSession(net::io_context& ioc, ssl::context& ctx, const std::string& botToken,
                           const std::string& wsUrl) {
    WssUrl parsed = parseWssUrl(wsUrl);

    tcp::resolver resolver{ioc};
    auto const results = resolver.resolve(parsed.host, "443");

    websocket::stream<ssl::stream<tcp::socket>> ws{ioc, ctx};

    if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), parsed.host.c_str())) {
        throw beast::system_error(
            beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()));
    }

    net::connect(ws.next_layer().next_layer(), results.begin(), results.end());
    ws.next_layer().handshake(ssl::stream_base::client);

    ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
        req.set(http::field::user_agent, "boost-beast-slack-bot/1.0");
    }));

    ws.handshake(parsed.host, parsed.target);
    log("WebSocket handshake complete.");

    // --- ping/pong keepalive -------------------------------------------
    //
    // ws.read() runs synchronously and blocks the main thread until Slack
    // sends something, so a background thread drives the keepalive: it
    // sends a ping every kPingInterval and, if no pong has arrived within
    // kPongTimeout, treats the connection as dead and force-closes the
    // underlying socket. That close is what unblocks the main thread's
    // pending ws.read() (it returns with an error), which then falls
    // through to the normal reconnect path below.
    //
    // wsWriteMutex serializes ping frames against the ack frames written
    // in handleEnvelope, since both run on the same stream from different
    // threads. lastPong is updated from the control_callback, which Beast
    // invokes synchronously inside ws.read() on the main thread whenever a
    // pong frame arrives (Beast also auto-replies to any ping *we* receive
    // from Slack, with no extra code needed here).
    constexpr auto kPingInterval = std::chrono::seconds(30);
    constexpr auto kPongTimeout = std::chrono::seconds(60);

    std::mutex wsWriteMutex;
    std::atomic<std::chrono::steady_clock::time_point> lastPong{std::chrono::steady_clock::now()};

    ws.control_callback([&lastPong](websocket::frame_type kind, beast::string_view /*payload*/) {
        if (kind == websocket::frame_type::pong) {
            lastPong.store(std::chrono::steady_clock::now());
        }
    });

    std::mutex pingerMutex;
    std::condition_variable pingerCv;
    bool stopPinger = false;

    std::thread pinger([&] {
        auto lastPingAt = std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> lock(pingerMutex);
        while (!stopPinger) {
            pingerCv.wait_for(lock, std::chrono::seconds(1), [&] { return stopPinger; });
            if (stopPinger) break;

            auto now = std::chrono::steady_clock::now();

            if (now - lastPong.load() > kPongTimeout) {
                log("No pong received within timeout; closing socket to force reconnect.");
                beast::error_code ec;
                beast::get_lowest_layer(ws).close(ec);
                break;
            }

            if (now - lastPingAt >= kPingInterval) {
                lastPingAt = now;
                beast::error_code ec;
                {
                    std::lock_guard<std::mutex> wlock(wsWriteMutex);
                    ws.ping({}, ec);
                }
                if (ec) {
                    log("Ping failed: " + ec.message());
                }
            }
        }
    });

    bool shouldReconnect = false;
    beast::flat_buffer buffer;

    while (!shouldReconnect) {
        buffer.clear();
        beast::error_code ec;
        ws.read(buffer, ec);
        if (ec == websocket::error::closed) {
            log("WebSocket closed by peer.");
            break;
        }
        if (ec) {
            log("WebSocket read error: " + ec.message());
            break;
        }

        const std::string raw = beast::buffers_to_string(buffer.data());
        json::value envelope;
        try {
            envelope = json::parse(raw);
        } catch (const std::exception& e) {
            log(std::string("Failed to parse envelope JSON: ") + e.what());
            continue;
        }
        if (!envelope.is_object()) {
            continue;
        }

        try {
            handleEnvelope(ioc, ctx, botToken, ws, wsWriteMutex, envelope.as_object(), shouldReconnect);
        } catch (const std::exception& e) {
            log(std::string("Error handling envelope: ") + e.what());
        }
    }

    {
        std::lock_guard<std::mutex> lock(pingerMutex);
        stopPinger = true;
    }
    pingerCv.notify_all();
    pinger.join();

    beast::error_code ec;
    ws.close(websocket::close_code::normal, ec);
}

}  // namespace

int main() {
    const char* botTokenEnv = std::getenv("SLACK_BOT_TOKEN");
    const char* appTokenEnv = std::getenv("SLACK_APP_TOKEN");
    if (!botTokenEnv || !appTokenEnv) {
        std::cerr << "Set SLACK_BOT_TOKEN and SLACK_APP_TOKEN environment variables." << std::endl;
        return 1;
    }
    const std::string botToken = botTokenEnv;
    const std::string appToken = appTokenEnv;

    net::io_context ioc;
    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_peer);

    std::cout << "ArphaXAD is running in socket mode..." << std::endl;

    while (true) {
        try {
            std::string wsUrl = openSocketModeUrl(ioc, ctx, appToken);
            runSocketModeSession(ioc, ctx, botToken, wsUrl);
        } catch (const std::exception& e) {
            log(std::string("Session error: ") + e.what());
        }
        log("Reconnecting in 1s...");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

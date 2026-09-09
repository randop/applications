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
//   - WebSocket writes (acks + pings) are serialized via a boost::asio::strand
//     instead of a raw mutex.

#define BOOST_JSON_NO_LIB  // build Boost.JSON header-only from this one TU

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/json/src.hpp>  // header-only Boost.JSON implementation

#include <atomic>
#include <cstdlib>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <cctype>
#include <algorithm>

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

bool is_equals(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
               [](unsigned char c1, unsigned char c2) {
                   return std::tolower(c1) == std::tolower(c2);
               });
}

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

    std::string text = getStr(event, "text");
    const std::string channel = getStr(event, "channel");
    const std::string ts = getStr(event, "ts");
    const std::string client_msg_id = getStr(event, "client_msg_id");

    log("Received message: " + client_msg_id);

    const bool willReplyOnThread = false;

    json::object reply;
    reply["channel"] = channel;
    if (willReplyOnThread) {
        reply["thread_ts"] = ts;
    }

    if (is_equals(text, "ping")) {
      reply["text"] = "*pong*";
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
// All WebSocket writes are posted through `wsStrand` so they are serialized
// with the pinger's ping frames (Beast's websocket::stream is not safe for
// concurrent writes from multiple threads).
void handleEnvelope(net::io_context& ioc, ssl::context& ctx, const std::string& botToken,
                     websocket::stream<ssl::stream<tcp::socket>>& ws,
                     net::strand<net::io_context::executor_type>& wsStrand,
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
        // Run the write on the strand (serialized with pings). We block
        // until it completes so the ack is on the wire before we continue
        // handling the rest of the envelope — same timing as the old mutex.
        std::promise<void> done;
        auto fut = done.get_future();
        net::post(wsStrand, [&ws, ack = std::move(ack), p = std::move(done)]() mutable {
            beast::error_code ec;
            ws.write(net::buffer(json::serialize(json::value(ack))), ec);
            if (ec) {
                log("ack write failed: " + ec.message());
            }
            p.set_value();
        });
        fut.wait();
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

    // --- ping/pong keepalive (strand-serialized) -----------------------
    //
    // ws.read() runs synchronously and blocks the main thread until Slack
    // sends something. A steady_timer + strand drives the keepalive:
    //   - every kPingInterval a ping is posted onto the strand;
    //   - if no pong arrives within kPongTimeout the lowest-layer socket
    //     is closed, which unblocks the pending ws.read() so the session
    //     falls through to the normal reconnect path.
    //
    // The same strand also serializes ack writes from handleEnvelope, so
    // Beast's websocket::stream never sees concurrent writes.
    // lastPong is updated from the control_callback, which Beast invokes
    // synchronously inside ws.read() on the main thread whenever a pong
    // frame arrives (Beast also auto-replies to any ping *we* receive
    // from Slack, with no extra code needed here).
    constexpr auto kPingInterval = std::chrono::seconds(30);
    constexpr auto kPongTimeout = std::chrono::seconds(60);

    // Strand that owns all writes to `ws`.
    net::strand<net::io_context::executor_type> wsStrand{ioc.get_executor()};

    std::atomic<std::chrono::steady_clock::time_point> lastPong{
        std::chrono::steady_clock::now()};

    ws.control_callback([&lastPong](websocket::frame_type kind, beast::string_view /*payload*/) {
        if (kind == websocket::frame_type::pong) {
            lastPong.store(std::chrono::steady_clock::now());
        }
    });

    // Shared flag so the timer can be cancelled cleanly when the session ends.
    auto stopPinger = std::make_shared<std::atomic<bool>>(false);
    auto pingTimer = std::make_shared<net::steady_timer>(ioc);
    auto lastPingAt = std::make_shared<std::chrono::steady_clock::time_point>(
        std::chrono::steady_clock::now());

    // Recursive lambda that schedules the next ping/timeout check.
    // Everything that touches `ws` runs on `wsStrand`.
    auto schedulePing = std::make_shared<std::function<void()>>();
    *schedulePing = [stopPinger, pingTimer, lastPingAt, schedulePing, &ws, &wsStrand, &lastPong,
                     kPingInterval, kPongTimeout]() {
        if (stopPinger->load()) return;

        pingTimer->expires_after(std::chrono::seconds(1));
        pingTimer->async_wait(net::bind_executor(
            wsStrand,
            [stopPinger, pingTimer, lastPingAt, schedulePing, &ws, &lastPong, kPingInterval,
             kPongTimeout](beast::error_code ec) {
                if (ec || stopPinger->load()) return;

                auto now = std::chrono::steady_clock::now();

                if (now - lastPong.load() > kPongTimeout) {
                    log("No pong received within timeout; closing socket to force reconnect.");
                    beast::error_code closeEc;
                    beast::get_lowest_layer(ws).close(closeEc);
                    return;
                }

                // Fire a ping every kPingInterval (we tick every 1 s).
                if (now - *lastPingAt >= kPingInterval) {
                    *lastPingAt = now;
                    beast::error_code pingEc;
                    ws.ping({}, pingEc);
                    if (pingEc) {
                        log("Ping failed: " + pingEc.message());
                    }
                }

                (*schedulePing)();
            }));
    };

    // Kick off the first timer.
    (*schedulePing)();

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
            handleEnvelope(ioc, ctx, botToken, ws, wsStrand, envelope.as_object(), shouldReconnect);
        } catch (const std::exception& e) {
            log(std::string("Error handling envelope: ") + e.what());
        }
    }

    // Stop the keepalive timer and drain any pending strand work before close.
    stopPinger->store(true);
    pingTimer->cancel();

    // Barrier: wait until the strand has finished any in-flight handler
    // (ping / timeout check) so we can safely close the socket.
    {
        std::promise<void> drained;
        auto fut = drained.get_future();
        net::post(wsStrand, [p = std::move(drained)]() mutable { p.set_value(); });
        fut.wait();
    }

    beast::error_code closeEc;
    ws.close(websocket::close_code::normal, closeEc);
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

    // Run the io_context on a background thread so the strand / timer
    // handlers can execute while the main thread blocks in ws.read().
    std::thread iocThread([&ioc] {
        net::executor_work_guard<net::io_context::executor_type> work(ioc.get_executor());
        ioc.run();
    });

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

    // Unreachable in the current design, but keep the thread join for
    // completeness if the loop is ever made exit-able.
    ioc.stop();
    iocThread.join();
}

// websocket_client.cpp
// Single-file production-quality C++17 Boost.Asio WebSocket client
// using a strand + stackful coroutines (boost::asio::spawn).
// Supports ws:// and wss:// endpoints, text and binary messages,
// and Boost.JSON payload convenience overloads (json::value and
// json::object).
//
// Build (example):
//   g++ -std=c++17 -O2 -pthread websocket_client.cpp \
//       -lboost_system -lboost_json -lssl -lcrypto -o websocket_client
//
// Requires: Boost >= 1.75 (Asio + Beast + JSON), OpenSSL
//
// Notes:
//   - Beast is used for WebSocket framing/protocol correctness rather than
//     reimplementing RFC 6455 framing by hand.
//   - Each websocket_client instance serializes all protocol operations on
//     its strand. Callers may invoke the public async_* functions from any
//     thread, exactly like https_client.
//   - A client owns one WebSocket connection at a time. Reconnect by calling
//     async_connect after a completed close/disconnect.
//   - Ping/pong, fragmentation and close frames are handled by Boost.Beast.
//
// API styles (both share the same do_* implementation):
//   1. Coroutine overloads taking asio::yield_context: sequential, linear
//      control flow — call them in order from inside asio::spawn and let
//      exceptions propagate, exactly like https_client::do_request.
//      They throw boost::system::system_error on failure.
//   2. Completion-token overloads: composable, error_code based handlers,
//      for cases where operations must be issued independently.

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/json.hpp>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace asio      = boost::asio;
namespace ssl       = asio::ssl;
namespace beast     = boost::beast;
namespace websocket = beast::websocket;
namespace json      = boost::json;

using tcp        = asio::ip::tcp;
using error_code = boost::system::error_code;

// Excludes the coroutine overloads from the completion-token templates below.
template <typename T>
using not_yield_t = std::enable_if_t<
    !std::is_same_v<std::decay_t<T>, asio::yield_context>, int>;

//------------------------------------------------------------------------------
// WebSocket endpoint
//------------------------------------------------------------------------------

struct websocket_endpoint {
    std::string scheme = "wss";
    std::string host;
    std::string port;
    std::string target = "/";

    bool secure() const
    {
        return scheme == "wss";
    }

    std::string effective_port() const
    {
        if (!port.empty())
            return port;
        return secure() ? "443" : "80";
    }

    // Accepts:
    //   ws://host[:port][/target][?query]
    //   wss://host[:port][/target][?query]
    //   ws://[::1]:port/target        (IPv6 literals must be bracketed)
    static websocket_endpoint parse(std::string_view uri)
    {
        websocket_endpoint ep;

        const auto scheme_pos = uri.find("://");
        if (scheme_pos == std::string_view::npos)
            throw std::invalid_argument("WebSocket URI must contain ://");

        ep.scheme = std::string(uri.substr(0, scheme_pos));
        if (ep.scheme != "ws" && ep.scheme != "wss")
            throw std::invalid_argument("WebSocket URI scheme must be ws or wss");

        const auto rest = uri.substr(scheme_pos + 3);

        // The authority ends at the first path character or query string.
        const auto path_pos = rest.find_first_of("/?");
        const std::string_view authority =
            path_pos == std::string_view::npos ? rest : rest.substr(0, path_pos);

        ep.target = path_pos == std::string_view::npos
            ? "/"
            : std::string(rest.substr(path_pos));

        if (authority.empty())
            throw std::invalid_argument("WebSocket URI host is empty");

        if (authority.front() == '[') {
            const auto close = authority.find(']');
            if (close == std::string_view::npos)
                throw std::invalid_argument("Malformed IPv6 WebSocket URI");

            ep.host = std::string(authority.substr(1, close - 1));

            if (close + 1 < authority.size()) {
                if (authority[close + 1] != ':')
                    throw std::invalid_argument("Malformed WebSocket URI port");
                ep.port = std::string(authority.substr(close + 2));
            }
        } else {
            const auto colon = authority.rfind(':');
            if (colon != std::string_view::npos &&
                authority.find(':') == colon) {
                // Exactly one colon -> host:port.
                ep.host = std::string(authority.substr(0, colon));
                ep.port = std::string(authority.substr(colon + 1));
            } else {
                // No port, or an unbracketed IPv6 literal (accepted as-is).
                ep.host = std::string(authority);
            }
        }

        if (ep.host.empty())
            throw std::invalid_argument("WebSocket URI host is empty");

        for (char c : ep.port) {
            if (c < '0' || c > '9')
                throw std::invalid_argument("WebSocket URI port must be numeric");
        }

        return ep;
    }
};

//------------------------------------------------------------------------------
// Message types
//------------------------------------------------------------------------------

enum class websocket_message_type {
    text,
    binary
};

struct websocket_message {
    websocket_message_type type = websocket_message_type::text;
    std::string payload;
};

//------------------------------------------------------------------------------
// WebSocket client
//
// Mirrors the design of https_client:
//   * All protocol operations are serialized on the client's strand.
//   * Coroutine overloads throw boost::system::system_error on failure;
//     completion-token overloads convert the same exceptions into
//     error_codes so handlers always fire.
//   * The client is non-copyable; callbacks/coroutines capture `this`. The
//     caller must guarantee the client outlives the io_context::run() call.
//------------------------------------------------------------------------------

class websocket_client {
public:
    explicit websocket_client(asio::io_context& ioc)
        : ioc_(ioc)
        , strand_(asio::make_strand(ioc))
        , ssl_ctx_(ssl::context::tls_client)
    {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(ssl::verify_peer);
    }

    websocket_client(const websocket_client&) = delete;
    websocket_client& operator=(const websocket_client&) = delete;

    //--------------------------------------------------------------------------
    // Connect
    //--------------------------------------------------------------------------
    // Coroutine style: sequential, throws boost::system::system_error.
    void async_connect(const websocket_endpoint& endpoint,
                       asio::yield_context yield)
    {
        do_connect(endpoint, yield);
    }

    void async_connect(std::string_view uri, asio::yield_context yield)
    {
        do_connect(websocket_endpoint::parse(uri), yield);
    }

    // Completion-token style: void(error_code).
    template <typename CompletionToken, not_yield_t<CompletionToken> = 0>
    auto async_connect(websocket_endpoint endpoint, CompletionToken&& token)
    {
        return asio::async_initiate<CompletionToken, void(error_code)>(
            [this](auto handler, websocket_endpoint ep) {
                asio::spawn(strand_,
                    [this, h = std::move(handler), ep = std::move(ep)]
                    (asio::yield_context yield) mutable
                {
                    error_code ec;
                    try {
                        do_connect(std::move(ep), yield);
                    } catch (const boost::system::system_error& e) {
                        ec = e.code();
                    } catch (const std::exception&) {
                        ec = asio::error::fault;
                    }
                    std::move(h)(ec);
                },
                asio::detached);
            },
            token,
            std::move(endpoint));
    }

    template <typename CompletionToken, not_yield_t<CompletionToken> = 0>
    auto async_connect(std::string_view uri, CompletionToken&& token)
    {
        websocket_endpoint ep = websocket_endpoint::parse(uri);
        return async_connect(std::move(ep),
                             std::forward<CompletionToken>(token));
    }

    //--------------------------------------------------------------------------
    // Send
    //--------------------------------------------------------------------------
    // Coroutine style: sequential, throws boost::system::system_error.
    void async_send_text(std::string message, asio::yield_context yield)
    {
        do_send(websocket_message{websocket_message_type::text,
                                  std::move(message)},
                yield);
    }

    void async_send_binary(std::string data, asio::yield_context yield)
    {
        do_send(websocket_message{websocket_message_type::binary,
                                  std::move(data)},
                yield);
    }

    void async_send(websocket_message message, asio::yield_context yield)
    {
        do_send(std::move(message), yield);
    }

    void async_send_json(const json::value& value, asio::yield_context yield)
    {
        do_send(websocket_message{websocket_message_type::text,
                                  json::serialize(value)},
                yield);
    }

    void async_send_json(const json::object& object, asio::yield_context yield)
    {
        do_send(websocket_message{websocket_message_type::text,
                                  json::serialize(object)},
                yield);
    }

    // Completion-token style: void(error_code).
    template <typename CompletionToken, not_yield_t<CompletionToken> = 0>
    auto async_send_text(std::string message, CompletionToken&& token)
    {
        return async_send(
            websocket_message{websocket_message_type::text, std::move(message)},
            std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken, not_yield_t<CompletionToken> = 0>
    auto async_send_binary(std::string data, CompletionToken&& token)
    {
        return async_send(
            websocket_message{websocket_message_type::binary, std::move(data)},
            std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken, not_yield_t<CompletionToken> = 0>
    auto async_send(websocket_message message, CompletionToken&& token)
    {
        return asio::async_initiate<CompletionToken, void(error_code)>(
            [this](auto handler, websocket_message msg) {
                asio::spawn(strand_,
                    [this, h = std::move(handler), msg = std::move(msg)]
                    (asio::yield_context yield) mutable
                {
                    error_code ec;
                    try {
                        do_send(std::move(msg), yield);
                    } catch (const boost::system::system_error& e) {
                        ec = e.code();
                    } catch (const std::exception&) {
                        ec = asio::error::fault;
                    }
                    std::move(h)(ec);
                },
                asio::detached);
            },
            token,
            std::move(message));
    }

    template <typename CompletionToken, not_yield_t<CompletionToken> = 0>
    auto async_send_json(const json::value& value, CompletionToken&& token)
    {
        return async_send_text(json::serialize(value),
                               std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken, not_yield_t<CompletionToken> = 0>
    auto async_send_json(const json::object& object, CompletionToken&& token)
    {
        return async_send_text(json::serialize(object),
                               std::forward<CompletionToken>(token));
    }

    //--------------------------------------------------------------------------
    // Receive one complete WebSocket message.
    //
    // Beast handles fragmentation internally and completes this operation
    // only after the complete message has been assembled.
    //--------------------------------------------------------------------------
    // Coroutine style: sequential, throws boost::system::system_error.
    websocket_message async_receive(asio::yield_context yield)
    {
        return do_receive(yield);
    }

    // Completion-token style: void(error_code, websocket_message).
    template <typename CompletionToken, not_yield_t<CompletionToken> = 0>
    auto async_receive(CompletionToken&& token)
    {
        return asio::async_initiate<CompletionToken,
                                    void(error_code, websocket_message)>(
            [this](auto handler) {
                asio::spawn(strand_,
                    [this, h = std::move(handler)]
                    (asio::yield_context yield) mutable
                {
                    error_code ec;
                    websocket_message message;

                    try {
                        message = do_receive(yield);
                    } catch (const boost::system::system_error& e) {
                        ec = e.code();
                    } catch (const std::exception&) {
                        ec = asio::error::fault;
                    }

                    std::move(h)(ec, std::move(message));
                },
                asio::detached);
            },
            token);
    }

    //--------------------------------------------------------------------------
    // Graceful WebSocket close
    //--------------------------------------------------------------------------
    // Coroutine style: sequential, throws boost::system::system_error.
    // If the connection is not open, throws asio::error::not_connected.
    void async_close(asio::yield_context yield)
    {
        do_close(websocket::close_reason{websocket::close_code::normal},
                 yield);
    }

    void async_close(websocket::close_reason reason,
                     asio::yield_context yield)
    {
        do_close(reason, yield);
    }

    // Completion-token style: void(error_code).
    template <typename CompletionToken, not_yield_t<CompletionToken> = 0>
    auto async_close(CompletionToken&& token)
    {
        return async_close(
            websocket::close_reason{websocket::close_code::normal},
            std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken, not_yield_t<CompletionToken> = 0>
    auto async_close(websocket::close_reason reason, CompletionToken&& token)
    {
        return asio::async_initiate<CompletionToken, void(error_code)>(
            [this](auto handler, websocket::close_reason close_reason) {
                asio::spawn(strand_,
                    [this, h = std::move(handler), close_reason]
                    (asio::yield_context yield) mutable
                {
                    error_code ec;
                    try {
                        do_close(close_reason, yield);
                    } catch (const boost::system::system_error& e) {
                        ec = e.code();
                    } catch (const std::exception&) {
                        ec = asio::error::fault;
                    }
                    std::move(h)(ec);
                },
                asio::detached);
            },
            token,
            reason);
    }

    // Strand-serialized state: only meaningful as an indicator, since the
    // connection may transition to closed on the strand at any time.
    bool is_open() const
    {
        return open_;
    }

private:
    // A WebSocket stream is either:
    //   websocket::stream<tcp::socket>                    (ws)
    // or:
    //   websocket::stream<ssl::stream<tcp::socket>>       (wss)
    using plain_stream = websocket::stream<tcp::socket>;
    using tls_stream   = websocket::stream<ssl::stream<tcp::socket>>;

    void do_connect(websocket_endpoint ep, asio::yield_context yield)
    {
        error_code ec;

        // Always start a connect from a clean transport.
        open_ = false;
        plain_.reset();
        tls_.reset();

        tcp::resolver resolver(strand_);
        auto endpoints =
            resolver.async_resolve(ep.host, ep.effective_port(), yield[ec]);
        if (ec)
            throw boost::system::system_error(ec, "resolve");

        if (ep.secure()) {
            auto ws = std::make_unique<tls_stream>(strand_, ssl_ctx_);

            // SNI is required by many hosted WebSocket endpoints.
            if (!SSL_set_tlsext_host_name(
                    ws->next_layer().native_handle(), ep.host.c_str())) {
                throw boost::system::system_error(
                    static_cast<int>(::ERR_get_error()),
                    asio::error::get_ssl_category(),
                    "SNI");
            }

            asio::async_connect(ws->next_layer().next_layer(),
                                endpoints, yield[ec]);
            if (ec)
                throw boost::system::system_error(ec, "connect");

            ws->next_layer().async_handshake(ssl::stream_base::client,
                                             yield[ec]);
            if (ec)
                throw boost::system::system_error(ec, "TLS handshake");

            configure_stream(*ws);

            ws->async_handshake(ep.host, ep.target, yield[ec]);
            if (ec)
                throw boost::system::system_error(ec, "WebSocket handshake");

            tls_ = std::move(ws);
        } else {
            auto ws = std::make_unique<plain_stream>(strand_);

            asio::async_connect(ws->next_layer(), endpoints, yield[ec]);
            if (ec)
                throw boost::system::system_error(ec, "connect");

            configure_stream(*ws);

            ws->async_handshake(ep.host, ep.target, yield[ec]);
            if (ec)
                throw boost::system::system_error(ec, "WebSocket handshake");

            plain_ = std::move(ws);
        }

        open_ = true;
    }

    template <typename Stream>
    static void configure_stream(Stream& ws)
    {
        ws.set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::client));

        ws.set_option(
            websocket::stream_base::decorator(
                [](websocket::request_type& req) {
                    req.set(
                        beast::http::field::user_agent,
                        "asio-websocket-client/1.1");
                }));
    }

    void do_send(websocket_message message, asio::yield_context yield)
    {
        ensure_open();

        error_code ec;

        if (tls_) {
            tls_->text(message.type == websocket_message_type::text);
            tls_->async_write(asio::buffer(message.payload), yield[ec]);
        } else {
            plain_->text(message.type == websocket_message_type::text);
            plain_->async_write(asio::buffer(message.payload), yield[ec]);
        }

        if (ec) {
            open_ = false;
            throw boost::system::system_error(ec, "WebSocket write");
        }
    }

    websocket_message do_receive(asio::yield_context yield)
    {
        ensure_open();

        error_code ec;

        if (tls_)
            tls_->async_read(read_buffer_, yield[ec]);
        else
            plain_->async_read(read_buffer_, yield[ec]);

        if (ec) {
            open_ = false;
            throw boost::system::system_error(ec, "WebSocket read");
        }

        websocket_message message;

        // Query the message type before consuming the read buffer.
        const bool text = tls_ ? tls_->got_text() : plain_->got_text();

        message.type = text ? websocket_message_type::text
                            : websocket_message_type::binary;
        message.payload = beast::buffers_to_string(read_buffer_.data());

        // Reuse the buffer's capacity for the next message.
        read_buffer_.consume(read_buffer_.size());

        return message;
    }

    void do_close(websocket::close_reason reason,
                  asio::yield_context yield)
    {
        ensure_open();

        error_code ec;

        if (tls_)
            tls_->async_close(reason, yield[ec]);
        else
            plain_->async_close(reason, yield[ec]);

        // Best-effort TLS shutdown so the peer sees close_notify (wss only).
        if (tls_) {
            error_code ignored;
            tls_->next_layer().async_shutdown(yield[ignored]);
        }

        open_ = false;
        plain_.reset();
        tls_.reset();

        if (ec)
            throw boost::system::system_error(ec, "WebSocket close");
    }

    void ensure_open() const
    {
        if (!open_)
            throw boost::system::system_error(
                asio::error::not_connected,
                "WebSocket is not connected");
    }

    asio::io_context& ioc_;
    asio::strand<asio::io_context::executor_type> strand_;
    ssl::context ssl_ctx_;
    std::unique_ptr<plain_stream> plain_;
    std::unique_ptr<tls_stream> tls_;
    beast::flat_buffer read_buffer_;
    bool open_ = false;
};

//------------------------------------------------------------------------------
// Demo
//------------------------------------------------------------------------------
//
// The whole session is one linear coroutine — the same control flow as
// https_client::do_request: connect -> send -> receive -> close, with a
// single exception handler at the bottom instead of nested callbacks.
//
// Replace the echo endpoint with the service used by your application.
// All operations serialize on the client's internal strand.
//------------------------------------------------------------------------------

int main()
{
    try {
        asio::io_context ioc;
        websocket_client client(ioc);

        asio::spawn(ioc, [&client](asio::yield_context yield) {
            try {
                client.async_connect("wss://echo.websocket.events/", yield);
                std::cout << "connected\n";

                json::object payload;
                payload["message"] = "hello";
                payload["client"]  = "asio";
                payload["version"] = 1.0;

                client.async_send_json(payload, yield);
                std::cout << "message sent\n";

                websocket_message message = client.async_receive(yield);

                std::cout
                    << (message.type == websocket_message_type::text
                            ? "text"
                            : "binary")
                    << " message: "
                    << message.payload
                    << "\n";

                client.async_close(yield);
                std::cout << "closed\n";
            }
            catch (const boost::system::system_error& e) {
                std::cerr << "error: " << e.what() << "\n";
            }
        }, asio::detached);

        ioc.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

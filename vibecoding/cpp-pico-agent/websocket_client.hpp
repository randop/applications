// websocket_client.hpp
// C++17 Boost.Asio WebSocket client using a strand + stackful coroutines
// (boost::asio::spawn).
//
// Supports:
//   * ws:// and wss:// endpoints
//   * DNS resolution, TCP connect, optional TLS + SNI + peer verification
//   * WebSocket client handshake
//   * Text and binary messages
//   * JSON value/object convenience overloads
//   * Serialized writes through a strand
//   * Graceful close
//   * Ping/pong and framing handled by Boost.Beast
//   * Error-code based completion handlers
//
// Requires: Boost >= 1.75 (Asio + Beast + JSON), OpenSSL
//
// Notes:
//   - Beast is used for WebSocket framing/protocol correctness rather than
//     reimplementing RFC 6455 framing by hand.
//   - Each websocket_client instance serializes all protocol operations on its
//     strand. Callers may invoke the public async_* functions from any thread.
//   - A client owns one WebSocket connection at a time. Reconnect by calling
//     async_connect after a completed close/disconnect.

#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace json = boost::json;

using tcp = asio::ip::tcp;
using error_code = boost::system::error_code;

//------------------------------------------------------------------------------
// WebSocket endpoint
//------------------------------------------------------------------------------

struct websocket_endpoint {
    std::string scheme = "wss";
    std::string host;
    std::string port = "443";
    std::string target = "/";

    bool secure() const
    {
        return scheme == "wss";
    }

    static websocket_endpoint parse(std::string_view uri)
    {
        websocket_endpoint ep;

        const auto scheme_pos = uri.find("://");
        if (scheme_pos == std::string_view::npos)
            throw std::invalid_argument("WebSocket URI must contain ://");

        ep.scheme = std::string(uri.substr(0, scheme_pos));
        if (ep.scheme != "ws" && ep.scheme != "wss")
            throw std::invalid_argument("WebSocket URI scheme must be ws or wss");

        auto authority = uri.substr(scheme_pos + 3);
        const auto slash = authority.find('/');
        std::string_view host_port =
            slash == std::string_view::npos ? authority : authority.substr(0, slash);

        ep.target = slash == std::string_view::npos
            ? "/"
            : std::string(authority.substr(slash));

        if (host_port.empty())
            throw std::invalid_argument("WebSocket URI host is empty");

        bool port_specified = false;

        // This intentionally supports normal DNS names and IPv4 endpoints.
        // IPv6 literals must be written as [::1]:port.
        if (host_port.front() == '[') {
            const auto close = host_port.find(']');
            if (close == std::string_view::npos)
                throw std::invalid_argument("Malformed IPv6 WebSocket URI");

            ep.host = std::string(host_port.substr(1, close - 1));

            if (close + 1 < host_port.size()) {
                if (host_port[close + 1] != ':')
                    throw std::invalid_argument("Malformed WebSocket URI port");
                ep.port = std::string(host_port.substr(close + 2));
                port_specified = true;
            }
        } else {
            const auto colon = host_port.rfind(':');
            if (colon != std::string_view::npos &&
                host_port.find(':') == colon) {
                ep.host = std::string(host_port.substr(0, colon));
                ep.port = std::string(host_port.substr(colon + 1));
                port_specified = true;
            } else {
                ep.host = std::string(host_port);
            }
        }

        if (ep.host.empty())
            throw std::invalid_argument("WebSocket URI host is empty");

        if (!port_specified) {
            ep.port = ep.secure() ? "443" : "80";
        }

        if (ep.port.empty())
            throw std::invalid_argument("WebSocket URI port is empty");

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
//------------------------------------------------------------------------------

class websocket_client : public std::enable_shared_from_this<websocket_client> {
public:
    using control_handler =
        std::function<void(websocket::frame_type, beast::string_view)>;

    explicit websocket_client(asio::io_context& ioc)
        : ioc_(ioc)
        , strand_(asio::make_strand(ioc))
        , ssl_ctx_(ssl::context::tls_client)
    {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(ssl::verify_peer);
    }

    void set_control_handler(control_handler handler)
    {
        control_handler_ = std::move(handler);
    }

    //--------------------------------------------------------------------------
    // Connect
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_connect(websocket_endpoint endpoint, CompletionToken&& token)
    {
        return asio::async_initiate<CompletionToken, void(error_code)>(
            [self = shared_from_this()](auto handler,
                                        websocket_endpoint ep) mutable {
                asio::spawn(
                    self->strand_,
                    [self, h = std::move(handler), ep = std::move(ep)]
                    (asio::yield_context yield) mutable {
                        error_code ec;
                        try {
                            self->do_connect(std::move(ep), yield);
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

    template <typename CompletionToken>
    auto async_connect(std::string_view uri, CompletionToken&& token)
    {
        websocket_endpoint ep = websocket_endpoint::parse(uri);
        return async_connect(std::move(ep),
                             std::forward<CompletionToken>(token));
    }

    //--------------------------------------------------------------------------
    // Send text
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_send_text(std::string message, CompletionToken&& token)
    {
        return async_send(
            websocket_message{websocket_message_type::text, std::move(message)},
            std::forward<CompletionToken>(token));
    }

    //--------------------------------------------------------------------------
    // Send binary
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_send_binary(std::string data, CompletionToken&& token)
    {
        return async_send(
            websocket_message{websocket_message_type::binary, std::move(data)},
            std::forward<CompletionToken>(token));
    }

    //--------------------------------------------------------------------------
    // Generic send
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_send(websocket_message message, CompletionToken&& token)
    {
        return asio::async_initiate<CompletionToken, void(error_code)>(
            [self = shared_from_this()](auto handler,
                                        websocket_message msg) mutable {
                asio::spawn(
                    self->strand_,
                    [self, h = std::move(handler), msg = std::move(msg)]
                    (asio::yield_context yield) mutable {
                        error_code ec;
                        try {
                            self->do_send(std::move(msg), yield);
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

    //--------------------------------------------------------------------------
    // Boost.JSON convenience overloads
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_send_json(const json::value& value, CompletionToken&& token)
    {
        return async_send_text(
            json::serialize(value),
            std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_send_json(const json::object& object, CompletionToken&& token)
    {
        return async_send_text(
            json::serialize(object),
            std::forward<CompletionToken>(token));
    }

    //--------------------------------------------------------------------------
    // Receive one complete WebSocket message.
    //
    // Completion signature:
    //   void(error_code, websocket_message)
    //
    // Beast handles fragmentation internally and completes this operation only
    // after the complete message has been assembled.
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_receive(CompletionToken&& token)
    {
        return asio::async_initiate<CompletionToken,
                                    void(error_code, websocket_message)>(
            [self = shared_from_this()](auto handler) mutable {
                asio::spawn(
                    self->strand_,
                    [self, h = std::move(handler)]
                    (asio::yield_context yield) mutable {
                        error_code ec;
                        websocket_message message;

                        try {
                            message = self->do_receive(yield);
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
    // Ping
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_ping(CompletionToken&& token)
    {
        return asio::async_initiate<CompletionToken, void(error_code)>(
            [self = shared_from_this()](auto handler) mutable {
                asio::spawn(
                    self->strand_,
                    [self, h = std::move(handler)]
                    (asio::yield_context yield) mutable {
                        error_code ec;
                        try {
                            self->do_ping(yield);
                        } catch (const boost::system::system_error& e) {
                            ec = e.code();
                        } catch (const std::exception&) {
                            ec = asio::error::fault;
                        }
                        std::move(h)(ec);
                    },
                    asio::detached);
            },
            token);
    }

    //--------------------------------------------------------------------------
    // Graceful WebSocket close
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_close(websocket::close_reason reason,
                     CompletionToken&& token)
    {
        return asio::async_initiate<CompletionToken, void(error_code)>(
            [self = shared_from_this()](auto handler,
                                        websocket::close_reason close_reason) mutable {
                asio::spawn(
                    self->strand_,
                    [self, h = std::move(handler), close_reason]
                    (asio::yield_context yield) mutable {
                        error_code ec;
                        try {
                            self->do_close(close_reason, yield);
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

    template <typename CompletionToken>
    auto async_close(CompletionToken&& token)
    {
        return async_close(
            websocket::close_reason{websocket::close_code::normal},
            std::forward<CompletionToken>(token));
    }

    bool is_open() const
    {
        return open_;
    }

private:
    //--------------------------------------------------------------------------
    // Transport abstraction
    //
    // A WebSocket stream is either:
    //   websocket::stream<tcp::socket>
    // or:
    //   websocket::stream<ssl::stream<tcp::socket>>
    //
    // std::variant keeps the public API simple while allowing ws/wss at runtime.
    //--------------------------------------------------------------------------
    using plain_stream = websocket::stream<tcp::socket>;
    using tls_stream = websocket::stream<ssl::stream<tcp::socket>>;

    struct transport {
        std::unique_ptr<plain_stream> plain;
        std::unique_ptr<tls_stream> tls;

        bool secure() const
        {
            return static_cast<bool>(tls);
        }
    };

    void do_connect(websocket_endpoint ep, asio::yield_context yield);
    void do_send(websocket_message message, asio::yield_context yield);
    websocket_message do_receive(asio::yield_context yield);
    void do_ping(asio::yield_context yield);
    void do_close(websocket::close_reason reason, asio::yield_context yield);
    void ensure_open() const;

    template <typename Stream>
    void configure_stream(Stream& ws, const websocket_endpoint&)
    {
        ws.set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::client));

        ws.set_option(
            websocket::stream_base::decorator(
                [](websocket::request_type& req) {
                    req.set(
                        beast::http::field::user_agent,
                        "asio-websocket-client/1.0");
                }));

        auto self = shared_from_this();
        ws.control_callback(
            [self](websocket::frame_type kind, beast::string_view payload) {
                if (self->control_handler_) {
                    self->control_handler_(kind, payload);
                }
            });
    }

    asio::io_context& ioc_;
    asio::strand<asio::io_context::executor_type> strand_;
    ssl::context ssl_ctx_;
    transport transport_;
    control_handler control_handler_;
    bool open_ = false;
};

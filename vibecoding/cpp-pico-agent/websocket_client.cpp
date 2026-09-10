// websocket_client.cpp
// Single-file production-quality C++17 Boost.Asio WebSocket client
// using strands + stackful coroutines (boost::asio::spawn).
// Supports ws:// and wss://, DNS resolution, optional TLS + SNI + peer verification,
// Boost.Beast WebSocket framing, text/binary messages, Boost.JSON convenience
// overloads, serialized operations on a strand, and graceful close.
//
// Build (example):
//   g++ -std=c++17 -O2 -pthread websocket_client.cpp \
//       -lboost_system -lboost_json -lssl -lcrypto -o websocket_client
//
// Requires: Boost >= 1.75 (Asio + Beast + JSON), OpenSSL

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/json.hpp>

#include <iostream>
#include <string>
#include <string_view>
#include <memory>
#include <stdexcept>

namespace asio = boost::asio;
namespace ssl  = asio::ssl;
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
    std::string port;
    std::string target = "/";

    bool secure() const { return scheme == "wss"; }

    static websocket_endpoint parse(std::string_view uri) {
        websocket_endpoint ep;

        const auto scheme_pos = uri.find("://");
        if (scheme_pos == std::string_view::npos)
            throw std::invalid_argument("WebSocket URI must contain ://");

        ep.scheme = std::string(uri.substr(0, scheme_pos));
        if (ep.scheme != "ws" && ep.scheme != "wss")
            throw std::invalid_argument("WebSocket URI scheme must be ws or wss");

        auto authority = uri.substr(scheme_pos + 3);
        const auto slash = authority.find('/');
        std::string_view host_port = (slash == std::string_view::npos)
                                         ? authority
                                         : authority.substr(0, slash);
        ep.target = (slash == std::string_view::npos)
                        ? "/"
                        : std::string(authority.substr(slash));

        if (host_port.empty())
            throw std::invalid_argument("WebSocket URI host is empty");

        if (host_port.front() == '[') {
            const auto close = host_port.find(']');
            if (close == std::string_view::npos)
                throw std::invalid_argument("Malformed IPv6 WebSocket URI");

            ep.host = std::string(host_port.substr(1, close - 1));

            if (close + 1 < host_port.size()) {
                if (host_port[close + 1] != ':')
                    throw std::invalid_argument("Malformed WebSocket URI port");
                ep.port = std::string(host_port.substr(close + 2));
            }
        } else {
            const auto colon = host_port.rfind(':');
            if (colon != std::string_view::npos && host_port.find(':') == colon) {
                ep.host = std::string(host_port.substr(0, colon));
                ep.port = std::string(host_port.substr(colon + 1));
            } else {
                ep.host = std::string(host_port);
            }
        }

        if (ep.host.empty())
            throw std::invalid_argument("WebSocket URI host is empty");

        if (ep.port.empty())
            ep.port = ep.secure() ? "443" : "80";

        return ep;
    }
};

//------------------------------------------------------------------------------
// Message types
//------------------------------------------------------------------------------
enum class websocket_message_type { text, binary };

struct websocket_message {
    websocket_message_type type = websocket_message_type::text;
    std::string payload;
};

//------------------------------------------------------------------------------
// WebSocket client
//------------------------------------------------------------------------------
class websocket_client : public std::enable_shared_from_this<websocket_client> {
public:
    explicit websocket_client(asio::io_context& ioc)
        : ioc_(ioc)
        , strand_(asio::make_strand(ioc))
        , ssl_ctx_(ssl::context::tls_client)
    {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(ssl::verify_peer);
    }

    //--------------------------------------------------------------------------
    // Connect
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_connect(websocket_endpoint endpoint, CompletionToken&& token) {
        return asio::async_initiate<CompletionToken, void(error_code)>(
            [self = shared_from_this()](auto handler, websocket_endpoint ep) mutable {
                asio::spawn(self->strand_,
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
    auto async_connect(std::string_view uri, CompletionToken&& token) {
        websocket_endpoint ep = websocket_endpoint::parse(uri);
        return async_connect(std::move(ep), std::forward<CompletionToken>(token));
    }

    //--------------------------------------------------------------------------
    // Send
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_send_text(std::string message, CompletionToken&& token) {
        return async_send(
            websocket_message{websocket_message_type::text, std::move(message)},
            std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_send_binary(std::string data, CompletionToken&& token) {
        return async_send(
            websocket_message{websocket_message_type::binary, std::move(data)},
            std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_send(websocket_message message, CompletionToken&& token) {
        return asio::async_initiate<CompletionToken, void(error_code)>(
            [self = shared_from_this()](auto handler, websocket_message msg) mutable {
                asio::spawn(self->strand_,
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
    auto async_send_json(const json::value& value, CompletionToken&& token) {
        return async_send_text(json::serialize(value), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_send_json(const json::object& object, CompletionToken&& token) {
        return async_send_text(json::serialize(object), std::forward<CompletionToken>(token));
    }

    //--------------------------------------------------------------------------
    // Receive
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_receive(CompletionToken&& token) {
        return asio::async_initiate<CompletionToken, void(error_code, websocket_message)>(
            [self = shared_from_this()](auto handler) mutable {
                asio::spawn(self->strand_,
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
    // Close
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_close(websocket::close_reason reason, CompletionToken&& token) {
        return asio::async_initiate<CompletionToken, void(error_code)>(
            [self = shared_from_this()](auto handler, websocket::close_reason r) mutable {
                asio::spawn(self->strand_,
                    [self, h = std::move(handler), r]
                    (asio::yield_context yield) mutable {
                        error_code ec;
                        try {
                            self->do_close(r, yield);
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
    auto async_close(CompletionToken&& token) {
        return async_close(websocket::close_reason{websocket::close_code::normal},
                           std::forward<CompletionToken>(token));
    }

    bool is_open() const { return open_; }

private:
    using plain_stream = websocket::stream<tcp::socket>;
    using tls_stream   = websocket::stream<ssl::stream<tcp::socket>>;

    struct transport {
        std::unique_ptr<plain_stream> plain;
        std::unique_ptr<tls_stream>   tls;
        bool secure() const { return static_cast<bool>(tls); }
    };

    void do_connect(websocket_endpoint ep, asio::yield_context yield) {
        error_code ec;

        open_ = false;
        transport_.plain.reset();
        transport_.tls.reset();

        tcp::resolver resolver(strand_);
        auto endpoints = resolver.async_resolve(ep.host, ep.port, yield[ec]);
        if (ec) throw boost::system::system_error(ec, "resolve");

        if (ep.secure()) {
            auto ws = std::make_unique<tls_stream>(strand_, ssl_ctx_);

            if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), ep.host.c_str())) {
                throw boost::system::system_error(
                    static_cast<int>(::ERR_get_error()),
                    asio::error::get_ssl_category(),
                    "SNI");
            }

            asio::async_connect(ws->next_layer().next_layer(), endpoints, yield[ec]);
            if (ec) throw boost::system::system_error(ec, "connect");

            ws->next_layer().async_handshake(ssl::stream_base::client, yield[ec]);
            if (ec) throw boost::system::system_error(ec, "TLS handshake");

            configure_stream(*ws, ep);
            ws->async_handshake(ep.host, ep.target, yield[ec]);
            if (ec) throw boost::system::system_error(ec, "WebSocket handshake");

            transport_.tls = std::move(ws);
        } else {
            auto ws = std::make_unique<plain_stream>(strand_);

            asio::async_connect(ws->next_layer(), endpoints, yield[ec]);
            if (ec) throw boost::system::system_error(ec, "connect");

            configure_stream(*ws, ep);
            ws->async_handshake(ep.host, ep.target, yield[ec]);
            if (ec) throw boost::system::system_error(ec, "WebSocket handshake");

            transport_.plain = std::move(ws);
        }

        open_ = true;
    }

    template <typename Stream>
    static void configure_stream(Stream& ws, const websocket_endpoint&) {
        ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(beast::http::field::user_agent, "asio-websocket-client/1.0");
            }));
    }

    void do_send(websocket_message message, asio::yield_context yield) {
        ensure_open();
        error_code ec;

        if (transport_.secure()) {
            auto& ws = *transport_.tls;
            ws.text(message.type == websocket_message_type::text);
            ws.async_write(asio::buffer(message.payload), yield[ec]);
        } else {
            auto& ws = *transport_.plain;
            ws.text(message.type == websocket_message_type::text);
            ws.async_write(asio::buffer(message.payload), yield[ec]);
        }

        if (ec) {
            open_ = false;
            throw boost::system::system_error(ec, "WebSocket write");
        }
    }

    websocket_message do_receive(asio::yield_context yield) {
        ensure_open();
        error_code ec;
        beast::flat_buffer buffer;

        if (transport_.secure()) {
            auto& ws = *transport_.tls;
            ws.async_read(buffer, yield[ec]);
            if (ec) {
                open_ = false;
                throw boost::system::system_error(ec, "WebSocket read");
            }
            return websocket_message{
                ws.got_text() ? websocket_message_type::text : websocket_message_type::binary,
                beast::buffers_to_string(buffer.data())
            };
        }

        auto& ws = *transport_.plain;
        ws.async_read(buffer, yield[ec]);
        if (ec) {
            open_ = false;
            throw boost::system::system_error(ec, "WebSocket read");
        }
        return websocket_message{
            ws.got_text() ? websocket_message_type::text : websocket_message_type::binary,
            beast::buffers_to_string(buffer.data())
        };
    }

    void do_close(websocket::close_reason reason, asio::yield_context yield) {
        if (!open_) return;

        error_code ec;
        if (transport_.secure()) {
            transport_.tls->async_close(reason, yield[ec]);
            transport_.tls.reset();
        } else {
            transport_.plain->async_close(reason, yield[ec]);
            transport_.plain.reset();
        }

        open_ = false;
        if (ec) throw boost::system::system_error(ec, "WebSocket close");
    }

    void ensure_open() const {
        if (!open_)
            throw boost::system::system_error(asio::error::not_connected, "WebSocket is not connected");
    }

    asio::io_context& ioc_;
    asio::strand<asio::io_context::executor_type> strand_;
    ssl::context ssl_ctx_;
    transport transport_;
    bool open_ = false;
};

//------------------------------------------------------------------------------
// Demo
//------------------------------------------------------------------------------
int main() {
    try {
        asio::io_context ioc;
        auto client = std::make_shared<websocket_client>(ioc);

        client->async_connect("wss://echo.websocket.events/",
            [client](error_code ec) {
                if (ec) {
                    std::cerr << "connect error: " << ec.message() << "\n";
                    return;
                }
                std::cout << "connected\n";

                json::object payload;
                payload["message"] = "hello";
                payload["client"]  = "asio";
                payload["version"] = 1.0;

                client->async_send_json(payload,
                    [client](error_code ec) {
                        if (ec) {
                            std::cerr << "send error: " << ec.message() << "\n";
                            return;
                        }
                        std::cout << "message sent\n";

                        client->async_receive(
                            [client](error_code ec, websocket_message message) {
                                if (ec) {
                                    std::cerr << "receive error: " << ec.message() << "\n";
                                    return;
                                }
                                std::cout << (message.type == websocket_message_type::text ? "text" : "binary")
                                          << " message: " << message.payload << "\n";

                                client->async_close(
                                    [client](error_code close_ec) {
                                        if (close_ec) {
                                            std::cerr << "close error: " << close_ec.message() << "\n";
                                            return;
                                        }
                                        std::cout << "closed\n";
                                    });
                            });
                    });
            });

        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

// websocket_client.cpp
// Non-template implementation for websocket_client.

#include "websocket_client.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

void websocket_client::do_connect(websocket_endpoint ep, asio::yield_context yield)
{
    error_code ec;

    // A reconnect always starts from a clean transport.
    open_ = false;
    transport_.plain.reset();
    transport_.tls.reset();

    tcp::resolver resolver(strand_);
    auto endpoints =
        resolver.async_resolve(ep.host, ep.port, yield[ec]);
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

        asio::async_connect(
            ws->next_layer().next_layer(),
            endpoints,
            yield[ec]);

        if (ec)
            throw boost::system::system_error(ec, "connect");

        ws->next_layer().async_handshake(
            ssl::stream_base::client,
            yield[ec]);

        if (ec)
            throw boost::system::system_error(ec, "TLS handshake");

        configure_stream(*ws, ep);

        ws->async_handshake(ep.host, ep.target, yield[ec]);
        if (ec)
            throw boost::system::system_error(ec, "WebSocket handshake");

        transport_.tls = std::move(ws);
    } else {
        auto ws = std::make_unique<plain_stream>(strand_);

        asio::async_connect(
            ws->next_layer(),
            endpoints,
            yield[ec]);

        if (ec)
            throw boost::system::system_error(ec, "connect");

        configure_stream(*ws, ep);

        ws->async_handshake(ep.host, ep.target, yield[ec]);
        if (ec)
            throw boost::system::system_error(ec, "WebSocket handshake");

        transport_.plain = std::move(ws);
    }

    open_ = true;
}

void websocket_client::do_send(websocket_message message, asio::yield_context yield)
{
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

websocket_message websocket_client::do_receive(asio::yield_context yield)
{
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
            ws.got_text()
                ? websocket_message_type::text
                : websocket_message_type::binary,
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
        ws.got_text()
            ? websocket_message_type::text
            : websocket_message_type::binary,
        beast::buffers_to_string(buffer.data())
    };
}

void websocket_client::do_ping(asio::yield_context yield)
{
    ensure_open();

    error_code ec;

    if (transport_.secure()) {
        transport_.tls->async_ping({}, yield[ec]);
    } else {
        transport_.plain->async_ping({}, yield[ec]);
    }

    if (ec) {
        open_ = false;
        throw boost::system::system_error(ec, "WebSocket ping");
    }
}

void websocket_client::do_close(websocket::close_reason reason,
                                asio::yield_context yield)
{
    if (!open_)
        return;

    error_code ec;

    if (transport_.secure()) {
        transport_.tls->async_close(reason, yield[ec]);
    } else {
        transport_.plain->async_close(reason, yield[ec]);
    }

    open_ = false;
    transport_.plain.reset();
    transport_.tls.reset();

    if (ec)
        throw boost::system::system_error(ec, "WebSocket close");
}

void websocket_client::ensure_open() const
{
    if (!open_)
        throw boost::system::system_error(
            asio::error::not_connected,
            "WebSocket is not connected");
}

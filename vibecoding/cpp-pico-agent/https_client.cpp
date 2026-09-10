// https_client.cpp
// Non-template implementation for https_client.

#include "https_client.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

http_response https_client::do_request(http_request req, asio::yield_context yield)
{
    error_code ec;

    tcp::resolver resolver(strand_);
    auto endpoints = resolver.async_resolve(req.host, req.port, yield[ec]);
    if (ec) throw boost::system::system_error(ec, "resolve");

    ssl::stream<tcp::socket> stream(strand_, ssl_ctx_);

    if (!SSL_set_tlsext_host_name(stream.native_handle(), req.host.c_str())) {
        throw boost::system::system_error(
            static_cast<int>(::ERR_get_error()),
            asio::error::get_ssl_category(),
            "SNI");
    }

    asio::async_connect(stream.next_layer(), endpoints, yield[ec]);
    if (ec) throw boost::system::system_error(ec, "connect");

    stream.async_handshake(ssl::stream_base::client, yield[ec]);
    if (ec) throw boost::system::system_error(ec, "handshake");

    // Build request
    std::string request;
    request.reserve(512 + req.body.size());

    request += to_string(req.method);
    request += ' ';
    request += req.target;
    request += " HTTP/1.1\r\n";
    request += "Host: ";
    request += req.host;
    request += "\r\n";
    request += "User-Agent: asio-https-client/1.1\r\n";
    request += "Accept: */*\r\n";
    request += "Connection: close\r\n";

    if (!req.body.empty() ||
        req.method == http_method::POST ||
        req.method == http_method::PUT  ||
        req.method == http_method::PATCH)
    {
        request += "Content-Length: ";
        request += std::to_string(req.body.size());
        request += "\r\n";
    }

    for (const auto& [k, v] : req.headers) {
        request += k;
        request += ": ";
        request += v;
        request += "\r\n";
    }
    request += "\r\n";
    request += req.body;

    asio::async_write(stream, asio::buffer(request), yield[ec]);
    if (ec) throw boost::system::system_error(ec, "write");

    std::string raw;
    {
        char buf[8192];
        for (;;) {
            std::size_t n = stream.async_read_some(asio::buffer(buf), yield[ec]);
            if (ec == asio::error::eof || ec == ssl::error::stream_truncated)
                break;
            if (ec) throw boost::system::system_error(ec, "read");
            raw.append(buf, n);
        }
    }

    stream.async_shutdown(yield[ec]); // best-effort

    return parse_response(std::move(raw));
}

http_response https_client::parse_response(std::string raw)
{
    http_response resp;

    auto pos = raw.find("\r\n");
    if (pos == std::string::npos)
        throw boost::system::system_error(asio::error::fault, "bad status line");

    std::string status_line = raw.substr(0, pos);
    raw.erase(0, pos + 2);

    auto sp1 = status_line.find(' ');
    auto sp2 = status_line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos)
        throw boost::system::system_error(asio::error::fault, "malformed status");

    resp.status_code = std::stoi(status_line.substr(sp1 + 1, sp2 - sp1 - 1));
    resp.status_message = status_line.substr(sp2 + 1);

    while (true) {
        pos = raw.find("\r\n");
        if (pos == std::string::npos) break;
        std::string line = raw.substr(0, pos);
        raw.erase(0, pos + 2);
        if (line.empty()) break;

        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            while (!val.empty() && (val[0] == ' ' || val[0] == '\t'))
                val.erase(0, 1);
            resp.headers[std::move(key)] = std::move(val);
        }
    }

    resp.body = std::move(raw);
    return resp;
}

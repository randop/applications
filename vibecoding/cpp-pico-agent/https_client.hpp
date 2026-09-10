// https_client.hpp
// C++17 Boost.Asio HTTPS client using strands + stackful coroutines
// (boost::asio::spawn). Supports standard HTTP verbs and Boost.JSON payload
// overloads (json::value and json::object).
//
// Requires: Boost >= 1.75 (Asio + JSON), OpenSSL

#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/json.hpp>

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace json = boost::json;
using tcp = asio::ip::tcp;
using error_code = boost::system::error_code;

//------------------------------------------------------------------------------
// HTTP method enumeration
//------------------------------------------------------------------------------
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
        case http_method::GET:     return "GET";
        case http_method::POST:    return "POST";
        case http_method::PUT:     return "PUT";
        case http_method::DELETE_: return "DELETE";
        case http_method::PATCH:   return "PATCH";
        case http_method::HEAD:    return "HEAD";
        case http_method::OPTIONS: return "OPTIONS";
        case http_method::TRACE:   return "TRACE";
        case http_method::CONNECT: return "CONNECT";
    }
    return "GET";
}

//------------------------------------------------------------------------------
// Request / Response types
//------------------------------------------------------------------------------
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

//------------------------------------------------------------------------------
// Minimal, high-quality HTTPS client
//------------------------------------------------------------------------------
class https_client {
public:
    explicit https_client(asio::io_context& ioc)
        : ioc_(ioc)
        , strand_(asio::make_strand(ioc))
        , ssl_ctx_(ssl::context::tls_client)
    {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(ssl::verify_peer);
    }

    // Generic async request. Completion signature: void(error_code, http_response)
    template <typename CompletionToken>
    auto async_request(http_request req, CompletionToken&& token)
    {
        return asio::async_initiate<CompletionToken, void(error_code, http_response)>(
            [this](auto handler, http_request req) {
                asio::spawn(strand_,
                    [this, h = std::move(handler), req = std::move(req)]
                    (asio::yield_context yield) mutable
                {
                    error_code ec;
                    http_response resp;
                    try {
                        resp = do_request(std::move(req), yield);
                    } catch (const boost::system::system_error& e) {
                        ec = e.code();
                    } catch (const std::exception&) {
                        ec = asio::error::fault;
                    }
                    std::move(h)(ec, std::move(resp));
                },
                asio::detached);
            },
            token,
            std::move(req));
    }

    //--------------------------------------------------------------------------
    // GET / DELETE / HEAD / OPTIONS / TRACE / CONNECT (no body)
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_get(std::string_view host, std::string_view target,
                   CompletionToken&& token, std::string_view port = "443")
    {
        http_request r;
        r.method = http_method::GET;
        r.host   = std::string(host);
        r.port   = std::string(port);
        r.target = std::string(target);
        return async_request(std::move(r), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_delete(std::string_view host, std::string_view target,
                      CompletionToken&& token, std::string_view port = "443")
    {
        http_request r;
        r.method = http_method::DELETE_;
        r.host   = std::string(host);
        r.port   = std::string(port);
        r.target = std::string(target);
        return async_request(std::move(r), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_head(std::string_view host, std::string_view target,
                    CompletionToken&& token, std::string_view port = "443")
    {
        http_request r;
        r.method = http_method::HEAD;
        r.host   = std::string(host);
        r.port   = std::string(port);
        r.target = std::string(target);
        return async_request(std::move(r), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_options(std::string_view host, std::string_view target,
                       CompletionToken&& token, std::string_view port = "443")
    {
        http_request r;
        r.method = http_method::OPTIONS;
        r.host   = std::string(host);
        r.port   = std::string(port);
        r.target = std::string(target);
        return async_request(std::move(r), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_trace(std::string_view host, std::string_view target,
                     CompletionToken&& token, std::string_view port = "443")
    {
        http_request r;
        r.method = http_method::TRACE;
        r.host   = std::string(host);
        r.port   = std::string(port);
        r.target = std::string(target);
        return async_request(std::move(r), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_connect_verb(std::string_view host, std::string_view target,
                            CompletionToken&& token, std::string_view port = "443")
    {
        http_request r;
        r.method = http_method::CONNECT;
        r.host   = std::string(host);
        r.port   = std::string(port);
        r.target = std::string(target);
        return async_request(std::move(r), std::forward<CompletionToken>(token));
    }

    //--------------------------------------------------------------------------
    // POST / PUT / PATCH – string body overloads
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_post(std::string_view host, std::string_view target,
                    std::string body, CompletionToken&& token,
                    std::string_view port = "443",
                    std::string_view content_type = "application/json")
    {
        http_request r;
        r.method = http_method::POST;
        r.host   = std::string(host);
        r.port   = std::string(port);
        r.target = std::string(target);
        r.body   = std::move(body);
        r.headers["Content-Type"] = std::string(content_type);
        return async_request(std::move(r), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_put(std::string_view host, std::string_view target,
                   std::string body, CompletionToken&& token,
                   std::string_view port = "443",
                   std::string_view content_type = "application/json")
    {
        http_request r;
        r.method = http_method::PUT;
        r.host   = std::string(host);
        r.port   = std::string(port);
        r.target = std::string(target);
        r.body   = std::move(body);
        r.headers["Content-Type"] = std::string(content_type);
        return async_request(std::move(r), std::forward<CompletionToken>(token));
    }

    template <typename CompletionToken>
    auto async_patch(std::string_view host, std::string_view target,
                     std::string body, CompletionToken&& token,
                     std::string_view port = "443",
                     std::string_view content_type = "application/json")
    {
        http_request r;
        r.method = http_method::PATCH;
        r.host   = std::string(host);
        r.port   = std::string(port);
        r.target = std::string(target);
        r.body   = std::move(body);
        r.headers["Content-Type"] = std::string(content_type);
        return async_request(std::move(r), std::forward<CompletionToken>(token));
    }

    //--------------------------------------------------------------------------
    // POST / PUT / PATCH – Boost.JSON value overloads
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_post(std::string_view host, std::string_view target,
                    const json::value& payload, CompletionToken&& token,
                    std::string_view port = "443")
    {
        return async_post(host, target,
                          json::serialize(payload),
                          std::forward<CompletionToken>(token),
                          port,
                          "application/json");
    }

    template <typename CompletionToken>
    auto async_put(std::string_view host, std::string_view target,
                   const json::value& payload, CompletionToken&& token,
                   std::string_view port = "443")
    {
        return async_put(host, target,
                         json::serialize(payload),
                         std::forward<CompletionToken>(token),
                         port,
                         "application/json");
    }

    template <typename CompletionToken>
    auto async_patch(std::string_view host, std::string_view target,
                     const json::value& payload, CompletionToken&& token,
                     std::string_view port = "443")
    {
        return async_patch(host, target,
                           json::serialize(payload),
                           std::forward<CompletionToken>(token),
                           port,
                           "application/json");
    }

    //--------------------------------------------------------------------------
    // POST / PUT / PATCH – Boost.JSON object overloads
    //--------------------------------------------------------------------------
    template <typename CompletionToken>
    auto async_post(std::string_view host, std::string_view target,
                    const json::object& payload, CompletionToken&& token,
                    std::string_view port = "443")
    {
        return async_post(host, target,
                          json::serialize(payload),
                          std::forward<CompletionToken>(token),
                          port,
                          "application/json");
    }

    template <typename CompletionToken>
    auto async_put(std::string_view host, std::string_view target,
                   const json::object& payload, CompletionToken&& token,
                   std::string_view port = "443")
    {
        return async_put(host, target,
                         json::serialize(payload),
                         std::forward<CompletionToken>(token),
                         port,
                         "application/json");
    }

    template <typename CompletionToken>
    auto async_patch(std::string_view host, std::string_view target,
                     const json::object& payload, CompletionToken&& token,
                     std::string_view port = "443")
    {
        return async_patch(host, target,
                           json::serialize(payload),
                           std::forward<CompletionToken>(token),
                           port,
                           "application/json");
    }

private:
    http_response do_request(http_request req, asio::yield_context yield);
    static http_response parse_response(std::string raw);

    asio::io_context& ioc_;
    asio::strand<asio::io_context::executor_type> strand_;
    ssl::context ssl_ctx_;
};

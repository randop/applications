// https_client.cpp
// Single-file production-quality C++17 Boost.Asio HTTPS client
// using strands + stackful coroutines (boost::asio::spawn).
// Supports all standard HTTP verbs + Boost.JSON payload overloads
// (json::value and json::object).
//
// Build (example):
//   g++ -std=c++17 -O2 -pthread https_client.cpp \
//       -lboost_system -lboost_json -lssl -lcrypto -o https_client
//
// Requires: Boost >= 1.75 (Asio + JSON), OpenSSL

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/json.hpp>
#include <iostream>
#include <string>
#include <string_view>
#include <map>
#include <utility>
#include <stdexcept>

namespace asio = boost::asio;
namespace ssl  = asio::ssl;
namespace json = boost::json;
using tcp      = asio::ip::tcp;
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
    http_response do_request(http_request req, asio::yield_context yield)
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

    static http_response parse_response(std::string raw)
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

    asio::io_context& ioc_;
    asio::strand<asio::io_context::executor_type> strand_;
    ssl::context ssl_ctx_;
};

//------------------------------------------------------------------------------
// Demo
//------------------------------------------------------------------------------
int main()
{
    try {
        asio::io_context ioc;
        https_client client(ioc);

        const std::string host = "httpbin.org";

        // 1. String body
        client.async_post(host, "/post",
            R"({"msg":"plain string"})",
            [](error_code ec, http_response resp) {
                if (ec) { std::cerr << "POST(string) error: " << ec.message() << "\n"; return; }
                std::cout << "POST(string) -> " << resp.status_code << "\n";
            });

        // 2. json::value
        json::value jv = {
            {"hello", "world"},
            {"answer", 42},
            {"nested", {{"a", true}, {"b", nullptr}}}
        };

        client.async_post(host, "/post", jv,
            [](error_code ec, http_response resp) {
                if (ec) { std::cerr << "POST(value) error: " << ec.message() << "\n"; return; }
                std::cout << "POST(value)  -> " << resp.status_code << "\n";
            });

        // 3. json::object (new overload)
        json::object jo;
        jo["name"]  = "asio-client";
        jo["version"] = 1.1;
        jo["features"] = json::array{"strand", "coroutine", "json"};

        client.async_post(host, "/post", jo,
            [](error_code ec, http_response resp) {
                if (ec) { std::cerr << "POST(object) error: " << ec.message() << "\n"; return; }
                std::cout << "POST(object) -> " << resp.status_code
                          << "  body size=" << resp.body.size() << "\n";
            });

        client.async_put(host, "/put", jo,
            [](error_code ec, http_response resp) {
                if (ec) { std::cerr << "PUT(object) error: " << ec.message() << "\n"; return; }
                std::cout << "PUT(object)  -> " << resp.status_code << "\n";
            });

        client.async_patch(host, "/patch", jo,
            [](error_code ec, http_response resp) {
                if (ec) { std::cerr << "PATCH(object) error: " << ec.message() << "\n"; return; }
                std::cout << "PATCH(object)-> " << resp.status_code << "\n";
            });

        ioc.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

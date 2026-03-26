#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/url.hpp>
#include <coroutine>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "dispatcher/abort_controller.hpp"

namespace task_dispatcher::net {

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

// HTTP response structure
struct HttpResponse {
    int status_code{0};
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::error_code error;
    bool aborted{false};
    
    bool success() const noexcept {
        return !error && !aborted && status_code >= 200 && status_code < 300;
    }
};

// Download progress callback
using DownloadProgressCallback = std::function<void(size_t downloaded, size_t total)>;

// HTTP client using Boost Beast with io_uring backend
class HttpClient : public std::enable_shared_from_this<HttpClient> {
public:
    HttpClient(asio::io_context& ioc, ssl::context& ssl_ctx);
    ~HttpClient();

    // Non-copyable
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Async HTTP GET request
    asio::awaitable<HttpResponse> get(
        std::string_view url,
        std::shared_ptr<AbortController> abort_controller = nullptr,
        DownloadProgressCallback progress = nullptr
    );

    // Async HTTP POST request
    asio::awaitable<HttpResponse> post(
        std::string_view url,
        std::string_view body,
        std::string_view content_type = "application/json",
        std::shared_ptr<AbortController> abort_controller = nullptr
    );

    // Download file with progress
    asio::awaitable<HttpResponse> download(
        std::string_view url,
        std::string_view output_path,
        std::shared_ptr<AbortController> abort_controller = nullptr,
        DownloadProgressCallback progress = nullptr
    );

    // Set request timeout
    void set_timeout(std::chrono::seconds timeout);

    // Set user agent
    void set_user_agent(std::string_view user_agent);

    // Add default header
    void add_default_header(std::string_view name, std::string_view value);

    // Clear default headers
    void clear_default_headers();

private:
    asio::awaitable<HttpResponse> perform_request(
        const boost::urls::url_view& url,
        http::request<http::string_body>& req,
        std::shared_ptr<AbortController> abort_controller,
        DownloadProgressCallback progress
    );

    asio::awaitable<void> resolve_and_connect(
        const boost::urls::url_view& url,
        beast::tcp_stream& stream,
        beast::ssl_stream<beast::tcp_stream>& ssl_stream,
        bool use_ssl,
        std::shared_ptr<AbortController> abort_controller
    );

    asio::io_context& ioc_;
    ssl::context& ssl_ctx_;
    std::chrono::seconds timeout_{30};
    std::string user_agent_{"TaskDispatcher/1.0"};
    std::vector<std::pair<std::string, std::string>> default_headers_;
};

// Utility function to parse URL
boost::urls::url_view parse_url(std::string_view url_str);

// Coroutine-based HTTP request with automatic cancellation
asio::awaitable<HttpResponse> async_http_get(
    asio::io_context& ioc,
    ssl::context& ssl_ctx,
    std::string_view url,
    std::shared_ptr<AbortController> abort_controller = nullptr
);

} // namespace task_dispatcher::net

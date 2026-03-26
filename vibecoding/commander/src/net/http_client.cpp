#include "net/http_client.hpp"

#include <fstream>
#include <iostream>

namespace task_dispatcher::net {

HttpClient::HttpClient(asio::io_context& ioc, ssl::context& ssl_ctx)
    : ioc_(ioc)
    , ssl_ctx_(ssl_ctx)
{
}

HttpClient::~HttpClient() = default;

asio::awaitable<HttpResponse> HttpClient::get(
    std::string_view url_str,
    std::shared_ptr<AbortController> abort_controller,
    DownloadProgressCallback progress
) {
    auto url = parse_url(url_str);
    
    // Build request
    http::request<http::string_body> req{http::verb::get, url.path(), 11};
    req.set(http::field::host, url.host());
    req.set(http::field::user_agent, user_agent_);
    
    for (const auto& [name, value] : default_headers_) {
        req.set(name, value);
    }
    
    if (abort_controller) {
        check_abort(abort_controller->signal());
    }
    
    co_return co_await perform_request(url, req, abort_controller, progress);
}

asio::awaitable<HttpResponse> HttpClient::post(
    std::string_view url_str,
    std::string_view body,
    std::string_view content_type,
    std::shared_ptr<AbortController> abort_controller
) {
    auto url = parse_url(url_str);
    
    http::request<http::string_body> req{http::verb::post, url.path(), 11};
    req.set(http::field::host, url.host());
    req.set(http::field::user_agent, user_agent_);
    req.set(http::field::content_type, content_type);
    req.body() = std::string(body);
    req.prepare_payload();
    
    for (const auto& [name, value] : default_headers_) {
        req.set(name, value);
    }
    
    if (abort_controller) {
        check_abort(abort_controller->signal());
    }
    
    co_return co_await perform_request(url, req, abort_controller, nullptr);
}

asio::awaitable<HttpResponse> HttpClient::download(
    std::string_view url_str,
    std::string_view output_path,
    std::shared_ptr<AbortController> abort_controller,
    DownloadProgressCallback progress
) {
    auto url = parse_url(url_str);
    
    http::request<http::string_body> req{http::verb::get, url.path(), 11};
    req.set(http::field::host, url.host());
    req.set(http::field::user_agent, user_agent_);
    
    // Open output file
    std::ofstream file(std::string(output_path), std::ios::binary);
    if (!file) {
        HttpResponse response;
        response.error = std::make_error_code(std::errc::io_error);
        co_return response;
    }
    
    bool use_ssl = (url.scheme() == "https");
    
    // Resolve and connect
    tcp::resolver resolver(ioc_);
    auto const results = co_await resolver.async_resolve(
        std::string(url.host()), 
        use_ssl ? "443" : "80",
        asio::use_awaitable
    );
    
    if (abort_controller && abort_controller->is_aborted()) {
        HttpResponse response;
        response.aborted = true;
        co_return response;
    }
    
    if (use_ssl) {
        beast::ssl_stream<beast::tcp_stream> stream(ioc_, ssl_ctx_);
        
        // Set SNI
        if (!SSL_set_tlsext_host_name(stream.native_handle(), std::string(url.host()).c_str())) {
            HttpResponse response;
            response.error = std::make_error_code(std::errc::protocol_error);
            co_return response;
        }
        
        beast::get_lowest_layer(stream).connect(results);
        co_await stream.async_handshake(ssl::stream_base::client, asio::use_awaitable);
        
        co_await http::async_write(stream, req, asio::use_awaitable);
        
        beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        co_await http::async_read(stream, buffer, res, asio::use_awaitable);
        
        // Write body to file
        auto& body = res.body();
        size_t total_size = body.size();
        size_t downloaded = 0;
        
        for (auto const& chunk : body.data()) {
            if (abort_controller && abort_controller->is_aborted()) {
                HttpResponse response;
                response.aborted = true;
                co_return response;
            }
            
            file.write(static_cast<const char*>(chunk.data()), chunk.size());
            downloaded += chunk.size();
            
            if (progress) {
                progress(downloaded, total_size);
            }
        }
        
        file.close();
        
        HttpResponse response;
        response.status_code = res.result_int();
        response.body = std::string(output_path);
        
        // Copy headers
        for (auto const& field : res.base()) {
            response.headers.emplace_back(std::string(field.name_string()), 
                                          std::string(field.value()));
        }
        
        co_await stream.async_shutdown(asio::use_awaitable);
        co_return response;
        
    } else {
        beast::tcp_stream stream(ioc_);
        co_await stream.async_connect(results, asio::use_awaitable);
        
        co_await http::async_write(stream, req, asio::use_awaitable);
        
        beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        co_await http::async_read(stream, buffer, res, asio::use_awaitable);
        
        // Write body to file
        auto& body = res.body();
        size_t total_size = body.size();
        size_t downloaded = 0;
        
        for (auto const& chunk : body.data()) {
            if (abort_controller && abort_controller->is_aborted()) {
                HttpResponse response;
                response.aborted = true;
                co_return response;
            }
            
            file.write(static_cast<const char*>(chunk.data()), chunk.size());
            downloaded += chunk.size();
            
            if (progress) {
                progress(downloaded, total_size);
            }
        }
        
        file.close();
        
        HttpResponse response;
        response.status_code = res.result_int();
        response.body = std::string(output_path);
        
        for (auto const& field : res.base()) {
            response.headers.emplace_back(std::string(field.name_string()), 
                                          std::string(field.value()));
        }
        
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        
        co_return response;
    }
}

void HttpClient::set_timeout(std::chrono::seconds timeout) {
    timeout_ = timeout;
}

void HttpClient::set_user_agent(std::string_view user_agent) {
    user_agent_ = std::string(user_agent);
}

void HttpClient::add_default_header(std::string_view name, std::string_view value) {
    default_headers_.emplace_back(std::string(name), std::string(value));
}

void HttpClient::clear_default_headers() {
    default_headers_.clear();
}

asio::awaitable<HttpResponse> HttpClient::perform_request(
    const boost::urls::url_view& url,
    http::request<http::string_body>& req,
    std::shared_ptr<AbortController> abort_controller,
    DownloadProgressCallback progress
) {
    HttpResponse response;
    
    try {
        bool use_ssl = (url.scheme() == "https");
        
        tcp::resolver resolver(ioc_);
        auto const results = co_await resolver.async_resolve(
            std::string(url.host()), 
            use_ssl ? "443" : "80",
            asio::use_awaitable
        );
        
        if (abort_controller && abort_controller->is_aborted()) {
            response.aborted = true;
            co_return response;
        }
        
        if (use_ssl) {
            beast::ssl_stream<beast::tcp_stream> stream(ioc_, ssl_ctx_);
            
            if (!SSL_set_tlsext_host_name(stream.native_handle(), 
                                         std::string(url.host()).c_str())) {
                response.error = std::make_error_code(std::errc::protocol_error);
                co_return response;
            }
            
            beast::get_lowest_layer(stream).connect(results);
            co_await stream.async_handshake(ssl::stream_base::client, asio::use_awaitable);
            
            co_await http::async_write(stream, req, asio::use_awaitable);
            
            beast::flat_buffer buffer;
            http::response<http::dynamic_body> res;
            co_await http::async_read(stream, buffer, res, asio::use_awaitable);
            
            response.status_code = res.result_int();
            response.body = beast::buffers_to_string(res.body().data());
            
            for (auto const& field : res.base()) {
                response.headers.emplace_back(std::string(field.name_string()), 
                                              std::string(field.value()));
            }
            
            co_await stream.async_shutdown(asio::use_awaitable);
            
        } else {
            beast::tcp_stream stream(ioc_);
            co_await stream.async_connect(results, asio::use_awaitable);
            
            co_await http::async_write(stream, req, asio::use_awaitable);
            
            beast::flat_buffer buffer;
            http::response<http::dynamic_body> res;
            co_await http::async_read(stream, buffer, res, asio::use_awaitable);
            
            response.status_code = res.result_int();
            response.body = beast::buffers_to_string(res.body().data());
            
            for (auto const& field : res.base()) {
                response.headers.emplace_back(std::string(field.name_string()), 
                                              std::string(field.value()));
            }
            
            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        }
        
    } catch (const boost::system::system_error& e) {
        response.error = e.code();
    } catch (const AbortError& e) {
        response.aborted = true;
        response.error = std::make_error_code(std::errc::operation_canceled);
    }
    
    co_return response;
}

boost::urls::url_view parse_url(std::string_view url_str) {
    auto result = boost::urls::parse_uri(url_str);
    if (!result) {
        throw std::invalid_argument("Invalid URL");
    }
    return *result;
}

asio::awaitable<HttpResponse> async_http_get(
    asio::io_context& ioc,
    ssl::context& ssl_ctx,
    std::string_view url,
    std::shared_ptr<AbortController> abort_controller
) {
    HttpClient client(ioc, ssl_ctx);
    co_return co_await client.get(url, abort_controller);
}

} // namespace task_dispatcher::net

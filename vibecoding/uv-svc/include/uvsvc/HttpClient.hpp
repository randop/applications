// HttpClient.hpp - HTTP client wrapper using tlsuv
#pragma once

#include <uv.h>
#include <tlsuv/http.h>
#include <functional>
#include <string>

namespace uvsvc {

class HttpClient {
public:
    enum class Endpoint { JSON, IP };
    using ResponseCallback = std::function<void(const std::string&)>;

    HttpClient();
    ~HttpClient();

    bool init(uv_loop_t* loop, const std::string& base_url);
    void request(Endpoint ep, ResponseCallback callback);
    void close();

private:
    static void httpBodyCallback(tlsuv_http_req_t* req, char* body, ssize_t len);
    void onResponse(tlsuv_http_resp_t* resp);
    void onBody(char* body, ssize_t len);
    void processCompleteResponse();

    uv_loop_t* loop_ = nullptr;
    std::string base_url_;
    tlsuv_http_t http_;
    std::string body_buffer_;
    Endpoint current_endpoint_ = Endpoint::JSON;
    ResponseCallback response_callback_;
};

} // namespace uvsvc

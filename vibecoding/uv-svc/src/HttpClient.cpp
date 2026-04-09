// HttpClient.cpp - HTTP client implementation
#include "uvsvc/HttpClient.hpp"
#include "uvsvc/SafeOutput.hpp"
#include "uvsvc/JsonParser.hpp"

namespace uvsvc {

HttpClient::HttpClient() = default;

HttpClient::~HttpClient() {
    close();
}

bool HttpClient::init(uv_loop_t* loop, const std::string& base_url) {
    loop_ = loop;
    base_url_ = base_url;

    int rc = tlsuv_http_init(loop_, &http_, base_url_.c_str());
    if (rc != 0) return false;

    tlsuv_http_idle_keepalive(&http_, 10000);
    tlsuv_http_connect_timeout(&http_, 5000);

    return true;
}

void HttpClient::request(Endpoint ep, ResponseCallback callback) {
    const char* path = (ep == Endpoint::JSON) ? "/json" : "/ip";
    current_endpoint_ = ep;
    response_callback_ = callback;

    LOG_INFO(std::string("[HTTP] Sending GET ") + path);

    auto* req = tlsuv_http_req(&http_, "GET", path,
        [](tlsuv_http_resp_t* resp, void* ctx) {
            static_cast<HttpClient*>(ctx)->onResponse(resp);
        }, this);

    // === Set custom User-Agent header ===
    tlsuv_http_req_header(req, "User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_7_5) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.0 Safari/605.1.15");
    tlsuv_http_req_header(req, "Accept", "text/html,application/xhtml+xml,application/xml,application/json;q=0.9,*/*;q=0.8");
    tlsuv_http_req_header(req, "Accept-Language", "en-US,en;q=0.9");
    tlsuv_http_req_header(req, "Accept-Encoding", "gzip, deflate, br");
    tlsuv_http_req_header(req, "Sec-Fetch-Site", "none");
    tlsuv_http_req_header(req, "Sec-Fetch-Mode", "navigate");
    tlsuv_http_req_header(req, "Sec-Fetch-Dest", "document");
    tlsuv_http_req_header(req, "Sec-Fetch-User", "?1");
    tlsuv_http_req_header(req, "Cache-Control", "max-age=0");
    tlsuv_http_req_header(req, "Upgrade-Insecure-Requests", "1");
    tlsuv_http_req_header(req, "Referer", "https://search.brave.com/");
    tlsuv_http_req_header(req, "DNT", "1");

    if (req) {
        req->data = this;
        req->resp.body_cb = [](tlsuv_http_req_t* r, char* body, ssize_t len) {
            auto* self = static_cast<HttpClient*>(r->data);
            self->onBody(body, len);
        };
    }
}

void HttpClient::close() {
    tlsuv_http_close(&http_, nullptr);
}

void HttpClient::onResponse(tlsuv_http_resp_t* resp) {
    std::ostringstream oss;
    oss << "[HTTP] Response: " << resp->code;
    if (resp->status) oss << " " << resp->status;
    LOG_INFO(oss.str());

    if (resp->code < 0) {
        LOG_ERROR(std::string("[HTTP] Error: ") + uv_strerror(resp->code));
    } else {
        LOG_INFO("[HTTP] Headers received");
    }
}

void HttpClient::onBody(char* body, ssize_t len) {
    if (len == UV_EOF) {
        processCompleteResponse();
    } else if (len < 0) {
        LOG_ERROR(std::string("[HTTP] Body error: ") + uv_strerror(len));
    } else if (len > 0) {
        body_buffer_.append(body, len);
    }
}

void HttpClient::processCompleteResponse() {
    if (current_endpoint_ == Endpoint::JSON) {
        JsonParser::parseHttpBinJson(body_buffer_);
    } else {
        JsonParser::parseHttpBinIp(body_buffer_);
    }

    if (response_callback_) {
        response_callback_(body_buffer_);
    }

    body_buffer_.clear();
}

} // namespace uvsvc

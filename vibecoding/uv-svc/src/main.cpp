// ============================================================================
// UV-SVC: HTTP Service with JSON Parsing
// A professional, modular HTTP client using libuv, tlsuv, and ArduinoJson
// ============================================================================

#include <uv.h>
#include <tlsuv/tlsuv.h>
#include <tlsuv/http.h>

#include <thread>
#include <atomic>
#include <iostream>
#include <chrono>
#include <vector>
#include <mutex>
#include <sstream>
#include <ctime>
#include <cstring>
#include <string>
#include <memory>
#include <functional>

// Third-party library
#include "../vendor/ArduinoJson.hpp"

// ============================================================================
// SECTION 1: Core Utilities (SafeOutput)
// ============================================================================
namespace uvsvc {

class SafeOutput {
public:
    static void print(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << msg << std::endl;
    }

    static std::string threadId() {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        return oss.str();
    }

    static std::mutex& getMutex() { return mutex_; }

private:
    static std::mutex mutex_;
};

std::mutex SafeOutput::mutex_;

#define LOG_INFO(msg) uvsvc::SafeOutput::print(std::string("[INFO] ") + (msg))
#define LOG_ERROR(msg) uvsvc::SafeOutput::print(std::string("[ERROR] ") + (msg))
#define LOG_THREAD(msg) uvsvc::SafeOutput::print("[" + uvsvc::SafeOutput::threadId() + "] " + (msg))

} // namespace uvsvc

// ============================================================================
// SECTION 2: JSON Parser
// ============================================================================
namespace uvsvc {

class JsonParser {
public:
    static void parseHttpBinJson(const std::string& json_str) {
        LOG_INFO("[JSON] === Parsing /json response ===");

        ArduinoJson::JsonDocument doc;
        auto error = ArduinoJson::deserializeJson(doc, json_str);

        if (error) {
            LOG_ERROR(std::string("[JSON] Parse error: ") + error.c_str());
            return;
        }

        auto slideshow = doc["slideshow"];
        if (!slideshow.isNull()) {
            LOG_INFO("[JSON] Slideshow found:");
            LOG_INFO(std::string("  Author: ") + (slideshow["author"] | "N/A"));
            LOG_INFO(std::string("  Date: ") + (slideshow["date"] | "N/A"));
            LOG_INFO(std::string("  Title: ") + (slideshow["title"] | "N/A"));

            auto slides = slideshow["slides"];
            if (slides.is<ArduinoJson::JsonArray>()) {
                LOG_INFO("  Slides:");
                ArduinoJson::JsonArray slidesArr = slides;
                for (auto slide : slidesArr) {
                    LOG_INFO(std::string("    - ") + (slide["title"] | "Untitled"));
                    LOG_INFO(std::string("      Type: ") + (slide["type"] | "unknown"));
                }
            }
        }

        LOG_INFO("[JSON] =================================");
    }

    static void parseHttpBinIp(const std::string& json_str) {
        LOG_INFO("[JSON] === Parsing /ip response ===");

        ArduinoJson::JsonDocument doc;
        auto error = ArduinoJson::deserializeJson(doc, json_str);

        if (error) {
            LOG_ERROR(std::string("[JSON] Parse error: ") + error.c_str());
            return;
        }

        const char* origin = doc["origin"];
        if (origin) {
            LOG_INFO(std::string("[JSON] Your IP: ") + origin);
        }

        LOG_INFO("[JSON] =================================");
    }
};

} // namespace uvsvc

// ============================================================================
// SECTION 3: Background Event Loop Worker
// ============================================================================
namespace uvsvc {

class BackgroundLoop {
public:
    explicit BackgroundLoop(std::string name) : name_(std::move(name)) {}

    ~BackgroundLoop() {
        stop();
        join();
    }

    BackgroundLoop(BackgroundLoop&& other) noexcept
        : name_(std::move(other.name_))
        , running_(other.running_.load())
        , thread_(std::move(other.thread_))
        , loop_(other.loop_)
        , stop_async_(other.stop_async_) {
        other.loop_ = nullptr;
        other.stop_async_ = nullptr;
    }

    BackgroundLoop& operator=(BackgroundLoop&& other) noexcept {
        if (this != &other) {
            stop();
            join();
            name_ = std::move(other.name_);
            running_ = other.running_.load();
            thread_ = std::move(other.thread_);
            loop_ = other.loop_;
            stop_async_ = other.stop_async_;
            other.loop_ = nullptr;
            other.stop_async_ = nullptr;
        }
        return *this;
    }

    BackgroundLoop(const BackgroundLoop&) = delete;
    BackgroundLoop& operator=(const BackgroundLoop&) = delete;

    void start() {
        running_ = true;
        thread_ = std::make_unique<std::thread>(&BackgroundLoop::run, this);
    }

    void stop() {
        running_ = false;
        if (stop_async_) {
            uv_async_send(stop_async_);
        }
    }

    void join() {
        if (thread_ && thread_->joinable()) {
            thread_->join();
        }
    }

private:
    void run() {
        LOG_THREAD(name_ + " background loop started");

        loop_ = uv_loop_new();
        if (!loop_) {
            LOG_ERROR(name_ + " failed to create loop");
            return;
        }

        stop_async_ = new uv_async_t;
        uv_async_init(loop_, stop_async_, [](uv_async_t* handle) {
            auto* self = static_cast<BackgroundLoop*>(handle->data);
            uv_stop(self->loop_);
        });
        stop_async_->data = this;

        uv_timer_t timer;
        uv_timer_init(loop_, &timer);
        timer.data = this;
        uv_timer_start(&timer, [](uv_timer_t* t) {
            auto* self = static_cast<BackgroundLoop*>(t->data);
            LOG_THREAD(self->name_ + " heartbeat");
        }, 1000, 1000);

        while (running_.load()) {
            uv_run(loop_, UV_RUN_DEFAULT);
        }

        LOG_THREAD(name_ + " loop stopping...");

        uv_timer_stop(&timer);
        uv_close((uv_handle_t*)&timer, nullptr);
        uv_close((uv_handle_t*)stop_async_, [](uv_handle_t* h) { delete h; });

        uv_loop_close(loop_);
        uv_loop_delete(loop_);

        LOG_THREAD(name_ + " background loop finished");
    }

    std::string name_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> thread_;
    uv_loop_t* loop_ = nullptr;
    uv_async_t* stop_async_ = nullptr;
};

} // namespace uvsvc

// ============================================================================
// SECTION 4: HTTP Client
// ============================================================================
namespace uvsvc {

class HttpClient {
public:
    enum class Endpoint { JSON, IP };
    using ResponseCallback = std::function<void(const std::string&)>;

    HttpClient() = default;
    ~HttpClient() { close(); }

    bool init(uv_loop_t* loop, const std::string& base_url) {
        loop_ = loop;
        base_url_ = base_url;

        int rc = tlsuv_http_init(loop_, &http_, base_url_.c_str());
        if (rc != 0) return false;

        tlsuv_http_idle_keepalive(&http_, 10000);
        tlsuv_http_connect_timeout(&http_, 5000);

        return true;
    }

    void request(Endpoint ep, ResponseCallback callback) {
        const char* path = (ep == Endpoint::JSON) ? "/json" : "/ip";
        current_endpoint_ = ep;
        response_callback_ = callback;

        LOG_INFO(std::string("[HTTP] Sending GET ") + path);

        auto* req = tlsuv_http_req(&http_, "GET", path,
            [](tlsuv_http_resp_t* resp, void* ctx) {
                static_cast<HttpClient*>(ctx)->onResponse(resp);
            }, this);

        if (req) {
            req->data = this;
            req->resp.body_cb = [](tlsuv_http_req_t* r, char* body, ssize_t len) {
                auto* self = static_cast<HttpClient*>(r->data);
                self->onBody(body, len);
            };
        }
    }

    void close() {
        tlsuv_http_close(&http_, nullptr);
    }

private:
    void onResponse(tlsuv_http_resp_t* resp) {
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

    void onBody(char* body, ssize_t len) {
        if (len == UV_EOF) {
            if (current_endpoint_ == Endpoint::JSON) {
                JsonParser::parseHttpBinJson(body_buffer_);
            } else {
                JsonParser::parseHttpBinIp(body_buffer_);
            }

            if (response_callback_) {
                response_callback_(body_buffer_);
            }

            body_buffer_.clear();
        } else if (len < 0) {
            LOG_ERROR(std::string("[HTTP] Body error: ") + uv_strerror(len));
        } else if (len > 0) {
            body_buffer_.append(body, len);
        }
    }

    uv_loop_t* loop_ = nullptr;
    std::string base_url_;
    tlsuv_http_t http_;
    std::string body_buffer_;
    Endpoint current_endpoint_ = Endpoint::JSON;
    ResponseCallback response_callback_;
};

} // namespace uvsvc

// ============================================================================
// SECTION 5: Application Logic
// ============================================================================
namespace uvsvc {

class Application {
public:
    void run() {
        LOG_THREAD("Application starting...");

        workers_.emplace_back("worker1");
        workers_.emplace_back("worker2");

        for (auto& w : workers_) {
            w.start();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        runHttpClient();

        LOG_THREAD("Shutting down workers...");
        for (auto& w : workers_) {
            w.stop();
        }
        for (auto& w : workers_) {
            w.join();
        }

        LOG_THREAD("Application finished");
    }

private:
    void runHttpClient() {
        LOG_THREAD("HTTP client starting");

        uv_loop_t* loop = uv_default_loop();

        HttpClient client;
        if (!client.init(loop, "https://httpbin.org")) {
            LOG_ERROR("[HTTP] Failed to initialize client");
            return;
        }

        client.request(HttpClient::Endpoint::JSON, [](const std::string&) {
            LOG_INFO("[HTTP] /json request completed");
        });

        uv_timer_t timer;
        uv_timer_init(loop, &timer);
        timer.data = &client;

        uv_timer_start(&timer, [](uv_timer_t* t) {
            auto* c = static_cast<HttpClient*>(t->data);
            c->request(HttpClient::Endpoint::IP, [](const std::string&) {
                LOG_INFO("[HTTP] /ip request completed");
            });
            uv_close((uv_handle_t*)t, nullptr);
        }, 3000, 0);

        uv_timer_t heartbeat;
        uv_timer_init(loop, &heartbeat);
        uv_timer_start(&heartbeat, [](uv_timer_t*) {
            LOG_THREAD("Main loop heartbeat");
        }, 2000, 2000);

        bool autostop = false;
        if (autostop) {
          uv_timer_t shutdown_timer;
          uv_timer_init(loop, &shutdown_timer);
          uv_timer_start(&shutdown_timer, [](uv_timer_t* t) {
              LOG_INFO("[HTTP] Auto-shutdown triggered");
              uv_stop(t->loop);
          }, 25000, 0);
        }

        uv_run(loop, UV_RUN_DEFAULT);

        uv_timer_stop(&heartbeat);
        uv_close((uv_handle_t*)&heartbeat, nullptr);
        client.close();

        uv_loop_close(loop);

        LOG_THREAD("HTTP client finished");
    }

    std::vector<BackgroundLoop> workers_;
};

} // namespace uvsvc

// ============================================================================
// SECTION 6: Entry Point
// ============================================================================
void tls_logger(int level, const char* file, unsigned int line, const char* msg) {
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    std::lock_guard<std::mutex> lock(uvsvc::SafeOutput::getMutex());
    std::cerr << "[" << spec.tv_sec << "." << spec.tv_nsec/1000000 << "] "
              << file << ":" << line << " " << msg << std::endl;
}

int main() {
    tlsuv_set_debug(1, tls_logger);

    uvsvc::Application app;
    app.run();

    return 0;
}

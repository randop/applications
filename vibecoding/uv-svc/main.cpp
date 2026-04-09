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

// Include ArduinoJson for JSON parsing
#include "vendor/ArduinoJson.hpp"

std::mutex cout_mutex;

// Helper to print from any thread
void safe_print(const std::string& msg) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << msg << std::endl;
}

// Helper to convert thread::id to string
std::string thread_id_str() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

// Structure to hold per-loop data
struct LoopData {
    uv_loop_t* loop = nullptr;
    uv_async_t* stop_async = nullptr;
    std::thread* thread = nullptr;
    std::atomic<bool> running{true};
    std::string name;
};

// HTTP client state
struct HttpClientData {
    tlsuv_http_t* http = nullptr;
    int request_count = 0;
    const int max_requests = 3;
    uv_timer_t* timer = nullptr;
    std::string response_buffer;  // Buffer to accumulate response body
    bool is_json_endpoint = true; // true for /json, false for /ip
};

static HttpClientData http_client_data;

// Logger for tlsuv
void tlsuv_logger(int level, const char *file, unsigned int line, const char *msg) {
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cerr << "[" << spec.tv_sec << "." << spec.tv_nsec/1000000 << "] "
              << file << ":" << line << " " << msg << std::endl;
}

// Parse and print JSON response from /json endpoint
void parse_json_response(const std::string& json_str) {
    safe_print("[JSON] Parsing /json response...");
    
    ArduinoJson::JsonDocument doc;
    ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(doc, json_str);
    
    if (error) {
        safe_print(std::string("[JSON] Failed to parse JSON: ") + error.c_str());
        return;
    }
    
    // Extract and print fields from httpbin.org/json
    // Example: {"slideshow": {"author": "Yours Truly", "date": "date of publication", ...}}
    
    safe_print("[JSON] === Parsed JSON Response ===");
    
    // Print all top-level keys
    for (ArduinoJson::JsonPair kv : doc.as<ArduinoJson::JsonObject>()) {
        safe_print(std::string("[JSON] Key: ") + kv.key().c_str());
        
        if (kv.value().is<ArduinoJson::JsonObject>()) {
            safe_print("[JSON]   Value is an object:");
            for (ArduinoJson::JsonPair inner : kv.value().as<ArduinoJson::JsonObject>()) {
                std::string value_str;
                if (inner.value().is<const char*>()) {
                    value_str = inner.value().as<const char*>();
                } else if (inner.value().is<int>()) {
                    value_str = std::to_string(inner.value().as<int>());
                } else if (inner.value().is<bool>()) {
                    value_str = inner.value().as<bool>() ? "true" : "false";
                } else {
                    value_str = "[complex type]";
                }
                safe_print(std::string("[JSON]     ") + inner.key().c_str() + ": " + value_str);
            }
        } else if (kv.value().is<const char*>()) {
            safe_print(std::string("[JSON]   Value: ") + kv.value().as<const char*>());
        }
    }
    safe_print("[JSON] =============================");
}

// Parse and print JSON response from /ip endpoint
void parse_ip_response(const std::string& json_str) {
    safe_print("[JSON] Parsing /ip response...");
    
    ArduinoJson::JsonDocument doc;
    ArduinoJson::DeserializationError error = ArduinoJson::deserializeJson(doc, json_str);
    
    if (error) {
        safe_print(std::string("[JSON] Failed to parse JSON: ") + error.c_str());
        return;
    }
    
    safe_print("[JSON] === Parsed IP Response ===");
    
    // /ip returns: {"origin": "xxx.xxx.xxx.xxx"}
    const char* origin = doc["origin"];
    if (origin) {
        safe_print(std::string("[JSON] Your IP address: ") + origin);
    } else {
        safe_print("[JSON] Could not find 'origin' field");
    }
    
    // Print all fields
    for (ArduinoJson::JsonPair kv : doc.as<ArduinoJson::JsonObject>()) {
        std::string value_str;
        if (kv.value().is<const char*>()) {
            value_str = kv.value().as<const char*>();
        } else {
            value_str = "[non-string value]";
        }
        safe_print(std::string("[JSON] ") + kv.key().c_str() + ": " + value_str);
    }
    safe_print("[JSON] =============================");
}

// HTTP body callback - receives response body data
void http_body_cb(tlsuv_http_req_t *req, char *body, ssize_t len) {
    if (len == UV_EOF) {
        // Response complete - process the accumulated JSON
        if (http_client_data.is_json_endpoint) {
            parse_json_response(http_client_data.response_buffer);
        } else {
            parse_ip_response(http_client_data.response_buffer);
        }
        
        // Clear buffer for next request
        http_client_data.response_buffer.clear();
        
        // Move to next request
        http_client_data.request_count++;
        
        if (http_client_data.request_count < http_client_data.max_requests) {
            // Schedule next request
            if (http_client_data.timer && http_client_data.http) {
                if (http_client_data.request_count == 1) {
                    // Switch to /ip endpoint
                    http_client_data.is_json_endpoint = false;
                    safe_print("\n[HTTP] Sending GET /ip request...");
                    tlsuv_http_req_t* req = tlsuv_http_req(http_client_data.http, "GET", "/ip", nullptr, nullptr);
                    if (req) {
                        req->resp.body_cb = http_body_cb;
                    }
                } else if (http_client_data.request_count == 2) {
                    // Back to /json
                    http_client_data.is_json_endpoint = true;
                    safe_print("\n[HTTP] Sending GET /json request...");
                    tlsuv_http_req_t* req = tlsuv_http_req(http_client_data.http, "GET", "/json", nullptr, nullptr);
                    if (req) {
                        req->resp.body_cb = http_body_cb;
                    }
                }
            }
        } else {
            // All requests done, close the client
            safe_print("\n[HTTP] All requests complete, closing client...");
            if (http_client_data.http) {
                tlsuv_http_close(http_client_data.http, [](tlsuv_http_t * http) {
                    safe_print("[HTTP] HTTP client is closed");
                });
            }
            if (http_client_data.timer) {
                uv_close((uv_handle_t*)http_client_data.timer, nullptr);
            }
        }
    } else if (len < 0) {
        std::ostringstream err;
        err << "[HTTP] Error receiving body: " << len << " (" << uv_strerror(len) << ")";
        safe_print(err.str());
    } else if (len > 0) {
        // Accumulate body content
        http_client_data.response_buffer.append(body, len);
    }
}

// HTTP response callback
static void on_http_response(tlsuv_http_resp_t *resp, void* ctx) {
    std::ostringstream oss;
    oss << resp->code;
    if (resp->status) {
        oss << " " << resp->status;
    }
    safe_print("[HTTP] Response: " + oss.str());

    // Print headers
    if (resp->code >= 0) {
        tlsuv_http_hdr *h;
        safe_print("[HTTP] Headers:");
        LIST_FOREACH(h, &resp->headers, _next) {
            safe_print(std::string("  ") + h->name + ": " + h->value);
        }
    } else {
        std::ostringstream err;
        err << "[HTTP] Error: " << resp->code << " (" << uv_strerror(resp->code) << ")";
        safe_print(err.str());
    }
}

// Background loop worker function
void background_loop_worker(LoopData* data) {
    safe_print("[" + thread_id_str() + "] " + data->name + " background loop started");

    data->loop = uv_loop_new();
    if (!data->loop) {
        safe_print("[" + thread_id_str() + "] " + data->name + " failed to create loop");
        return;
    }

    // Create stop async handle
    data->stop_async = new uv_async_t;
    uv_async_init(data->loop, data->stop_async, [](uv_async_t* handle) {
        LoopData* d = static_cast<LoopData*>(handle->data);
        safe_print("[" + thread_id_str() + "] Stop signal received in " + d->name);
        uv_stop(d->loop);
    });
    data->stop_async->data = data;

    // Example periodic timer
    uv_timer_t timer;
    uv_timer_init(data->loop, &timer);
    timer.data = data;

    uv_timer_start(&timer, [](uv_timer_t* t) {
        LoopData* d = static_cast<LoopData*>(t->data);
        safe_print("[" + thread_id_str() + "] " + d->name + " timer tick");
    }, 1000, 1000);

    // Run the loop
    while (data->running.load()) {
        uv_run(data->loop, UV_RUN_DEFAULT);
    }

    safe_print("[" + thread_id_str() + "] " + data->name + " loop stopping...");

    // Cleanup
    uv_timer_stop(&timer);
    uv_close((uv_handle_t*)&timer, nullptr);
    uv_close((uv_handle_t*)data->stop_async, [](uv_handle_t* h) { delete h; });

    uv_loop_close(data->loop);
    uv_loop_delete(data->loop);

    safe_print("[" + thread_id_str() + "] " + data->name + " background loop finished");
}

// Main thread loop with HTTP client
void main_loop_with_http() {
    safe_print("[" + thread_id_str() + "] Main loop started with HTTP client");

    uv_loop_t* loop = uv_default_loop();

    // Initialize HTTP client
    static tlsuv_http_t http;
    tlsuv_http_init(loop, &http, "https://httpbin.org");
    tlsuv_http_idle_keepalive(&http, 10000);
    tlsuv_http_connect_timeout(&http, 5000);

    // Store references for callbacks
    http_client_data.http = &http;

    // Setup timer for HTTP requests
    static uv_timer_t http_timer;
    uv_timer_init(loop, &http_timer);
    http_timer.data = &http;
    http_client_data.timer = &http_timer;

    // Start first request immediately
    safe_print("[HTTP] Sending GET /json request...");
    tlsuv_http_req_t* req = tlsuv_http_req(&http, "GET", "/json", on_http_response, nullptr);
    if (req) {
        req->resp.body_cb = http_body_cb;
    }

    // Main loop timer
    uv_timer_t main_timer;
    uv_timer_init(loop, &main_timer);
    uv_timer_start(&main_timer, [](uv_timer_t*) {
        safe_print("[" + thread_id_str() + "] Main loop timer tick");
    }, 1500, 1500);

    // Run the loop
    uv_run(loop, UV_RUN_DEFAULT);

    // Cleanup
    uv_timer_stop(&main_timer);
    uv_close((uv_handle_t*)&main_timer, nullptr);

    uv_loop_close(loop);

    safe_print("[" + thread_id_str() + "] Main loop finished");
}

int main() {
    // Set up tlsuv debug logging
    tlsuv_set_debug(1, tlsuv_logger);

    safe_print("[" + thread_id_str() + "] Program starting...");

    std::vector<LoopData*> background_loops(2);

    std::vector<std::string> thread_names = {"http_server", "http_client"};

    for (int i = 0; i < 2; ++i) {
        background_loops[i] = new LoopData();
        background_loops[i]->name = thread_names[i];
        background_loops[i]->thread = new std::thread(background_loop_worker, background_loops[i]);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Run main loop with HTTP client
    main_loop_with_http();

    // Graceful shutdown
    safe_print("[" + thread_id_str() + "] Shutting down background loops...");

    for (auto* data : background_loops) {
        data->running = false;
        if (data->stop_async) {
            uv_async_send(data->stop_async);
        }
    }

    for (auto* data : background_loops) {
        if (data->thread && data->thread->joinable()) {
            data->thread->join();
        }
        delete data->thread;
        delete data;
    }

    safe_print("[" + thread_id_str() + "] All loops stopped. Program exiting.");
    return 0;
}

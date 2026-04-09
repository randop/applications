// Application.cpp - Main application implementation
#include "uvsvc/Application.hpp"
#include "uvsvc/SafeOutput.hpp"
#include "uvsvc/HttpClient.hpp"

#include <tlsuv/tlsuv.h>
#include <thread>
#include <chrono>

namespace uvsvc {

void Application::run() {
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

void Application::runHttpClient() {
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
    }, 1000, 10000);

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

} // namespace uvsvc

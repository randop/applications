// BackgroundLoop.hpp - Background event loop worker
#pragma once

#include <uv.h>
#include <thread>
#include <atomic>
#include <memory>
#include <string>

namespace uvsvc {

class BackgroundLoop {
public:
    explicit BackgroundLoop(std::string name);
    ~BackgroundLoop();

    BackgroundLoop(BackgroundLoop&& other) noexcept;
    BackgroundLoop& operator=(BackgroundLoop&& other) noexcept;

    BackgroundLoop(const BackgroundLoop&) = delete;
    BackgroundLoop& operator=(const BackgroundLoop&) = delete;

    void start();
    void stop();
    void join();

private:
    void run();
    static void onStopAsync(uv_async_t* handle);
    static void onTimerTick(uv_timer_t* handle);

    std::string name_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> thread_;
    uv_loop_t* loop_ = nullptr;
    uv_async_t* stop_async_ = nullptr;
};

} // namespace uvsvc

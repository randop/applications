// BackgroundLoop.cpp - Background event loop implementation
#include "uvsvc/BackgroundLoop.hpp"
#include "uvsvc/SafeOutput.hpp"

namespace uvsvc {

BackgroundLoop::BackgroundLoop(std::string name) : name_(std::move(name)) {}

BackgroundLoop::~BackgroundLoop() {
    stop();
    join();
}

BackgroundLoop::BackgroundLoop(BackgroundLoop&& other) noexcept
    : name_(std::move(other.name_))
    , running_(other.running_.load())
    , thread_(std::move(other.thread_))
    , loop_(other.loop_)
    , stop_async_(other.stop_async_) {
    other.loop_ = nullptr;
    other.stop_async_ = nullptr;
}

BackgroundLoop& BackgroundLoop::operator=(BackgroundLoop&& other) noexcept {
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

void BackgroundLoop::start() {
    running_ = true;
    thread_ = std::make_unique<std::thread>(&BackgroundLoop::run, this);
}

void BackgroundLoop::stop() {
    running_ = false;
    if (stop_async_) {
        uv_async_send(stop_async_);
    }
}

void BackgroundLoop::join() {
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
}

void BackgroundLoop::onStopAsync(uv_async_t* handle) {
    auto* self = static_cast<BackgroundLoop*>(handle->data);
    uv_stop(self->loop_);
}

void BackgroundLoop::onTimerTick(uv_timer_t* handle) {
    auto* self = static_cast<BackgroundLoop*>(handle->data);
    LOG_THREAD(self->name_ + " heartbeat");
}

void BackgroundLoop::run() {
    LOG_THREAD(name_ + " background loop started");

    loop_ = uv_loop_new();
    if (!loop_) {
        LOG_ERROR(name_ + " failed to create loop");
        return;
    }

    stop_async_ = new uv_async_t;
    uv_async_init(loop_, stop_async_, onStopAsync);
    stop_async_->data = this;

    uv_timer_t timer;
    uv_timer_init(loop_, &timer);
    timer.data = this;
    uv_timer_start(&timer, onTimerTick, 1000, 10000);

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

} // namespace uvsvc

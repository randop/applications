#include "utils/timeout.hpp"

#include <iostream>

namespace task_dispatcher::utils {

TimeoutManager::TimeoutManager(asio::io_context& ioc)
    : ioc_(ioc)
{
}

std::shared_ptr<asio::steady_timer> TimeoutManager::create_timer(
    std::chrono::milliseconds duration,
    std::function<void(const std::error_code&)> handler
) {
    auto timer = std::make_shared<asio::steady_timer>(ioc_);
    timer->expires_after(duration);
    timer->async_wait([handler](const std::error_code& ec) {
        handler(ec);
    });
    
    std::lock_guard<std::mutex> lock(timers_mutex_);
    timers_.push_back(timer);
    
    // Clean up expired timers
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
            [](const auto& t) { return t.expired(); }),
        timers_.end()
    );
    
    return timer;
}

void TimeoutManager::cancel_all() {
    std::lock_guard<std::mutex> lock(timers_mutex_);
    for (auto& weak_timer : timers_) {
        if (auto timer = weak_timer.lock()) {
            std::error_code ec;
            timer->cancel(ec);
        }
    }
    timers_.clear();
}

TimeoutGuard::TimeoutGuard(
    asio::io_context& ioc,
    std::chrono::milliseconds duration,
    std::function<void()> on_timeout
)
    : on_timeout_(std::move(on_timeout))
{
    timer_ = std::make_shared<asio::steady_timer>(ioc);
    timer_->expires_after(duration);
    timer_->async_wait([this](const std::error_code& ec) {
        if (!ec) {
            timed_out_.store(true, std::memory_order_release);
            if (on_timeout_) {
                on_timeout_();
            }
        }
    });
}

TimeoutGuard::~TimeoutGuard() {
    cancel();
}

void TimeoutGuard::cancel() {
    if (timer_) {
        std::error_code ec;
        timer_->cancel(ec);
    }
}

void TimeoutGuard::reset(std::chrono::milliseconds duration) {
    if (timer_) {
        timer_->expires_after(duration);
    }
}

bool TimeoutGuard::timed_out() const noexcept {
    return timed_out_.load(std::memory_order_acquire);
}

std::optional<std::error_code> 
execute_with_deadline(
    asio::io_context& ioc,
    std::function<asio::awaitable<void>()> operation,
    std::chrono::milliseconds deadline,
    std::shared_ptr<AbortController> abort_controller
) {
    // This would need proper implementation with coroutine support
    // For now, return nullopt as placeholder
    return std::nullopt;
}

asio::awaitable<bool> sleep_for(
    asio::io_context& ioc,
    std::chrono::milliseconds duration,
    std::shared_ptr<AbortController> abort_controller
) {
    auto timer = std::make_shared<asio::steady_timer>(ioc);
    timer->expires_after(duration);
    
    // If abort controller provided, set up abort listener
    if (abort_controller) {
        auto signal = abort_controller->signal();
        if (signal) {
            signal->add_event_listener([timer](const std::string&) {
                std::error_code ec;
                timer->cancel(ec);
            });
        }
    }
    
    std::error_code ec;
    co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));
    
    // Check if aborted
    if (abort_controller && abort_controller->is_aborted()) {
        co_return false;
    }
    
    // If cancelled due to abort, return false
    if (ec == asio::error::operation_aborted) {
        co_return false;
    }
    
    co_return true;
}

} // namespace task_dispatcher::utils

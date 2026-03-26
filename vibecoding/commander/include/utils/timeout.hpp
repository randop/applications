#pragma once

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <coroutine>
#include <functional>
#include <memory>
#include <optional>
#include <system_error>

#include "dispatcher/abort_controller.hpp"

namespace task_dispatcher::utils {

namespace asio = boost::asio;

// Timeout configuration
struct TimeoutConfig {
    std::chrono::milliseconds operation_timeout{30000};  // 30s default
    std::chrono::milliseconds connect_timeout{10000};      // 10s default
    std::chrono::milliseconds read_timeout{30000};         // 30s default
    std::chrono::milliseconds write_timeout{30000};        // 30s default
};

// Timeout manager using boost::asio timers
class TimeoutManager {
public:
    explicit TimeoutManager(asio::io_context& ioc);
    ~TimeoutManager() = default;

    // Non-copyable
    TimeoutManager(const TimeoutManager&) = delete;
    TimeoutManager& operator=(const TimeoutManager&) = delete;

    // Create a timeout timer
    std::shared_ptr<asio::steady_timer> create_timer(
        std::chrono::milliseconds duration,
        std::function<void(const std::error_code&)> handler
    );

    // Cancel all timers
    void cancel_all();

    // Get reference to io_context
    asio::io_context& io_context() noexcept { return ioc_; }

private:
    asio::io_context& ioc_;
    std::vector<std::weak_ptr<asio::steady_timer>> timers_;
    std::mutex timers_mutex_;
};

// RAII timeout guard - automatically cancels on destruction
class TimeoutGuard {
public:
    TimeoutGuard(
        asio::io_context& ioc,
        std::chrono::milliseconds duration,
        std::function<void()> on_timeout
    );
    
    ~TimeoutGuard();

    // Cancel the timeout
    void cancel();

    // Reset/extend the timeout
    void reset(std::chrono::milliseconds duration);

    // Check if timed out
    bool timed_out() const noexcept;

private:
    std::shared_ptr<asio::steady_timer> timer_;
    std::atomic<bool> timed_out_{false};
    std::function<void()> on_timeout_;
};

// Awaitable wrapper with timeout support
template<typename Awaitable>
asio::awaitable<std::optional<typename Awaitable::value_type>> 
with_timeout(
    Awaitable awaitable,
    std::chrono::milliseconds timeout_duration,
    std::shared_ptr<AbortController> abort_controller = nullptr
) {
    using ValueType = typename Awaitable::value_type;
    
    auto& ioc = co_await asio::this_coro::executor;
    auto timer = std::make_shared<asio::steady_timer>(ioc);
    timer->expires_after(timeout_duration);
    
    std::optional<ValueType> result;
    std::error_code timeout_error;
    
    // Race between operation and timeout
    auto [order] = co_await asio::experimental::make_parallel_group(
        co_await asio::redirect_awaitable(awaitable)
            .then([&](auto... args) {
                if constexpr (sizeof...(args) == 1) {
                    result = std::get<0>(std::tuple{args...});
                }
            }),
        timer->async_wait(asio::deferred)
    ).async_wait(asio::wait_for_one(), asio::use_awaitable);
    
    if (order[0] == 1) {  // Timer completed first
        if (abort_controller) {
            abort_controller->abort("Operation timed out");
        }
        timeout_error = asio::error::timed_out;
        co_return std::nullopt;
    }
    
    timer->cancel();
    co_return result;
}

// Execute function with deadline
std::optional<std::error_code> 
execute_with_deadline(
    asio::io_context& ioc,
    std::function<asio::awaitable<void>()> operation,
    std::chrono::milliseconds deadline,
    std::shared_ptr<AbortController> abort_controller = nullptr
);

// Race multiple operations with timeout
template<typename... Awaitables>
asio::awaitable<std::variant<typename Awaitables::value_type...>>
race_with_timeout(
    std::chrono::milliseconds timeout,
    Awaitables... awaitables
) {
    auto& ioc = co_await asio::this_coro::executor;
    auto timer = std::make_shared<asio::steady_timer>(ioc);
    timer->expires_after(timeout);
    
    // Implementation would use asio::experimental::parallel_group
    // to race all operations including the timer
    co_return co_await make_parallel_group(
        std::move(awaitables)...,
        timer->async_wait(asio::deferred)
    ).async_wait(asio::wait_for_one(), asio::use_awaitable);
}

// Sleep with abort check
asio::awaitable<bool> sleep_for(
    asio::io_context& ioc,
    std::chrono::milliseconds duration,
    std::shared_ptr<AbortController> abort_controller = nullptr
);

} // namespace task_dispatcher::utils

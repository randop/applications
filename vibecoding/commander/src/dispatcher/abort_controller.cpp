#include "dispatcher/abort_controller.hpp"

#include <stdexcept>
#include <iostream>

namespace task_dispatcher {

// AbortSignal implementation
AbortSignal::AbortSignal() = default;

bool AbortSignal::aborted() const noexcept {
    return aborted_.load(std::memory_order_acquire);
}

std::optional<std::string> AbortSignal::reason() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return reason_;
}

void AbortSignal::add_event_listener(AbortHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.push_back(std::move(handler));
    
    // If already aborted, immediately invoke
    if (aborted_.load(std::memory_order_acquire)) {
        lock.unlock();
        handler(reason_.value_or("Unknown"));
    }
}

void AbortSignal::remove_event_listener(const AbortHandler& handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Note: This won't work perfectly with std::function comparison
    // In practice, you'd need a token-based system
    handlers_.erase(
        std::remove_if(handlers_.begin(), handlers_.end(),
            [&handler](const auto& h) {
                // Cannot compare std::function directly, use target
                return h.target_type() == handler.target_type();
            }),
        handlers_.end()
    );
}

void AbortSignal::throw_if_aborted() const {
    if (aborted()) {
        auto r = reason();
        throw AbortError(r.value_or("Operation aborted"));
    }
}

void AbortSignal::wait_for_abort() const {
    std::unique_lock<std::mutex> lock(mutex_);
    abort_cv_.wait(lock, [this] {
        return aborted_.load(std::memory_order_acquire);
    });
}

bool AbortSignal::wait_for_abort(std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> lock(mutex_);
    return abort_cv_.wait_for(lock, timeout, [this] {
        return aborted_.load(std::memory_order_acquire);
    });
}

void AbortSignal::abort(const std::string& reason) {
    std::vector<AbortHandler> handlers_to_invoke;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (aborted_.exchange(true, std::memory_order_acq_rel)) {
            return;  // Already aborted
        }
        reason_ = reason;
        handlers_to_invoke = handlers_;
    }
    
    // Notify all waiters
    abort_cv_.notify_all();
    
    // Invoke handlers outside of lock
    for (auto& handler : handlers_to_invoke) {
        try {
            handler(reason);
        } catch (const std::exception& e) {
            std::cerr << "Abort handler failed: " << e.what() << std::endl;
        }
    }
}

// AbortController implementation
AbortController::AbortController() 
    : signal_(std::make_shared<AbortSignal>()) 
{
}

AbortController::AbortController(std::chrono::milliseconds timeout)
    : signal_(std::make_shared<AbortSignal>()),
      timeout_(timeout)
{
    setup_timeout(timeout);
}

AbortController::~AbortController() {
    // Stop timeout thread if running
    timeout_active_.store(false, std::memory_order_release);
    if (timeout_thread_.joinable()) {
        timeout_thread_.join();
    }
}

std::shared_ptr<AbortController> AbortController::create() {
    return std::shared_ptr<AbortController>(new AbortController());
}

std::shared_ptr<AbortController> AbortController::create(std::chrono::milliseconds timeout) {
    return std::shared_ptr<AbortController>(new AbortController(timeout));
}

std::shared_ptr<AbortSignal> AbortController::signal() const noexcept {
    return signal_;
}

void AbortController::abort(const std::string& reason) {
    if (signal_) {
        signal_->abort(reason);
    }
}

void AbortController::abort(std::chrono::milliseconds timeout) {
    setup_timeout(timeout);
}

bool AbortController::is_aborted() const noexcept {
    return signal_ ? signal_->aborted() : false;
}

void AbortController::setup_timeout(std::chrono::milliseconds timeout) {
    // Stop existing timeout thread if any
    timeout_active_.store(false, std::memory_order_release);
    if (timeout_thread_.joinable()) {
        timeout_thread_.join();
    }
    
    timeout_active_.store(true, std::memory_order_release);
    timeout_thread_ = std::thread([this, timeout]() {
        auto start = std::chrono::steady_clock::now();
        auto end = start + timeout;
        
        while (timeout_active_.load(std::memory_order_acquire)) {
            auto now = std::chrono::steady_clock::now();
            if (now >= end) {
                this->abort("Operation timed out after " + 
                    std::to_string(timeout.count()) + "ms");
                break;
            }
            
            // Check periodically without busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

// AbortError implementation
AbortError::AbortError(std::string reason) 
    : reason_(std::move(reason)) 
{
}

const char* AbortError::what() const noexcept {
    return reason_.c_str();
}

const std::string& AbortError::reason() const noexcept {
    return reason_;
}

} // namespace task_dispatcher

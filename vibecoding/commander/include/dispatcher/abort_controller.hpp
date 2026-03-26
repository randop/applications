#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <thread>

namespace task_dispatcher {

// Abort signal - similar to DOM AbortSignal
class AbortSignal {
public:
    AbortSignal();
    
    bool aborted() const noexcept;
    std::optional<std::string> reason() const noexcept;
    
    // Add abort listener
    using AbortHandler = std::function<void(const std::string& reason)>;
    void add_event_listener(AbortHandler handler);
    void remove_event_listener(const AbortHandler& handler);
    
    // Throw if aborted
    void throw_if_aborted() const;

    // Wait for abort (blocking)
    void wait_for_abort() const;
    
    // Wait for abort with timeout
    bool wait_for_abort(std::chrono::milliseconds timeout) const;

private:
    friend class AbortController;
    
    void abort(const std::string& reason);
    
    mutable std::mutex mutex_;
    std::atomic<bool> aborted_{false};
    std::optional<std::string> reason_;
    std::vector<AbortHandler> handlers_;
    mutable std::condition_variable abort_cv_;
};

// Abort controller - similar to DOM AbortController
class AbortController : public std::enable_shared_from_this<AbortController> {
public:
    AbortController();
    explicit AbortController(std::chrono::milliseconds timeout);
    
    ~AbortController();

    // Factory method for shared_ptr
    static std::shared_ptr<AbortController> create();
    static std::shared_ptr<AbortController> create(std::chrono::milliseconds timeout);

    // Get the signal
    std::shared_ptr<AbortSignal> signal() const noexcept;

    // Trigger abort
    void abort(const std::string& reason = "Operation aborted");
    void abort(std::chrono::milliseconds timeout);

    // Check if already aborted
    bool is_aborted() const noexcept;

private:
    void setup_timeout(std::chrono::milliseconds timeout);
    
    std::shared_ptr<AbortSignal> signal_;
    std::optional<std::chrono::milliseconds> timeout_;
    std::atomic<bool> timeout_active_{false};
    std::thread timeout_thread_;
};

// Abort exception
class AbortError : public std::exception {
public:
    explicit AbortError(std::string reason);
    const char* what() const noexcept override;
    const std::string& reason() const noexcept;

private:
    std::string reason_;
};

// Utility for checking abort and converting to exception
inline void check_abort(const std::shared_ptr<AbortSignal>& signal) {
    if (signal && signal->aborted()) {
        signal->throw_if_aborted();
    }
}

} // namespace task_dispatcher

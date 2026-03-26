#pragma once

#include <coroutine>
#include <functional>
#include <memory>
#include <queue>
#include <mutex>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <thread>
#include <condition_variable>
#include <chrono>

namespace task_dispatcher {

class IoUringContext;
class AbortController;

struct Task {
    uint64_t id;
    std::function<void()> callback;
    std::shared_ptr<AbortController> abort_controller;
    bool is_cancelled{false};
};

class TaskDispatcher {
public:
    explicit TaskDispatcher(std::size_t thread_pool_size = 1);
    ~TaskDispatcher();

    // Non-copyable, movable
    TaskDispatcher(const TaskDispatcher&) = delete;
    TaskDispatcher& operator=(const TaskDispatcher&) = delete;
    TaskDispatcher(TaskDispatcher&&) = default;
    TaskDispatcher& operator=(TaskDispatcher&&) = default;

    // Event loop control
    void run();
    void stop();
    bool is_running() const noexcept;

    // Task scheduling
    uint64_t schedule(std::function<void()> task);
    uint64_t schedule_delayed(std::chrono::milliseconds delay, std::function<void()> task);
    
    // Task with abort support
    uint64_t schedule_with_abort(std::function<void(std::shared_ptr<AbortController>)> task,
                                 std::shared_ptr<AbortController> abort_controller);

    // Cancel task
    bool cancel(uint64_t task_id);
    
    // Post to event loop (thread-safe)
    void post(std::function<void()> callback);

    // Get io_uring context for async I/O
    IoUringContext& io_context() noexcept;

private:
    void process_events();
    void worker_thread();
    uint64_t generate_task_id();

    std::unique_ptr<IoUringContext> io_context_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> next_task_id_{1};
    
    std::mutex tasks_mutex_;
    std::queue<Task> pending_tasks_;
    std::unordered_map<uint64_t, Task> active_tasks_;
    
    std::vector<std::thread> worker_threads_;
    std::condition_variable tasks_cv_;
    std::mutex cv_mutex_;
};

// Awaitable for coroutine support
struct TaskAwaitable {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) const;
    void await_resume() const noexcept {}
    
    TaskDispatcher* dispatcher;
};

} // namespace task_dispatcher

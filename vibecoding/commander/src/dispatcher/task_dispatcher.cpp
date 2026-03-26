#include "dispatcher/task_dispatcher.hpp"
#include "dispatcher/io_uring_context.hpp"
#include "dispatcher/abort_controller.hpp"

#include <iostream>
#include <algorithm>

namespace task_dispatcher {

TaskDispatcher::TaskDispatcher(std::size_t thread_pool_size)
    : io_context_(std::make_unique<IoUringContext>(4096)) 
{
    // Start worker threads
    for (std::size_t i = 0; i < thread_pool_size; ++i) {
        worker_threads_.emplace_back(&TaskDispatcher::worker_thread, this);
    }
}

TaskDispatcher::~TaskDispatcher() {
    stop();
    for (auto& t : worker_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void TaskDispatcher::run() {
    running_ = true;
    
    // Initialize io_uring
    if (!io_context_->initialize()) {
        throw std::runtime_error("Failed to initialize io_uring");
    }
    
    // Main event loop - NodeJS style
    while (running_) {
        // Process timers and immediate callbacks
        process_events();
        
        // Poll for I/O completions
        io_context_->poll_completions();
        
        // Wait if no work available (efficient blocking)
        std::unique_lock<std::mutex> lock(cv_mutex_);
        tasks_cv_.wait_for(lock, std::chrono::milliseconds(1), [this] {
            std::lock_guard<std::mutex> tasks_lock(tasks_mutex_);
            return !pending_tasks_.empty() || !running_;
        });
    }
}

void TaskDispatcher::stop() {
    running_ = false;
    tasks_cv_.notify_all();
    io_context_->shutdown();
}

bool TaskDispatcher::is_running() const noexcept {
    return running_;
}

uint64_t TaskDispatcher::schedule(std::function<void()> task) {
    auto task_id = generate_task_id();
    
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    pending_tasks_.push(Task{
        .id = task_id,
        .callback = std::move(task),
        .abort_controller = nullptr,
        .is_cancelled = false
    });
    
    tasks_cv_.notify_one();
    return task_id;
}

uint64_t TaskDispatcher::schedule_delayed(
    std::chrono::milliseconds delay, 
    std::function<void()> task
) {
    auto task_id = generate_task_id();
    
    // Schedule timeout with io_uring
    __kernel_timespec ts{
        .tv_sec = static_cast<time_t>(delay.count() / 1000),
        .tv_nsec = static_cast<long>((delay.count() % 1000) * 1000000)
    };
    
    io_context_->async_timeout(&ts, 
        [this, task_id, task](int res, uint32_t flags) {
            if (res == 0) {
                // Timeout completed, schedule task
                schedule([task]() { task(); });
            }
        });
    
    return task_id;
}

uint64_t TaskDispatcher::schedule_with_abort(
    std::function<void(std::shared_ptr<AbortController>)> task,
    std::shared_ptr<AbortController> abort_controller
) {
    auto task_id = generate_task_id();
    
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    Task t{
        .id = task_id,
        .callback = [task, abort_controller]() {
            if (!abort_controller->is_aborted()) {
                task(abort_controller);
            }
        },
        .abort_controller = abort_controller,
        .is_cancelled = false
    };
    
    active_tasks_[task_id] = t;
    pending_tasks_.push(std::move(t));
    
    tasks_cv_.notify_one();
    return task_id;
}

bool TaskDispatcher::cancel(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    auto it = active_tasks_.find(task_id);
    if (it != active_tasks_.end()) {
        it->second.is_cancelled = true;
        if (it->second.abort_controller) {
            it->second.abort_controller->abort("Task cancelled by user");
        }
        return true;
    }
    
    // Check pending queue
    std::queue<Task> new_queue;
    bool found = false;
    
    while (!pending_tasks_.empty()) {
        auto task = std::move(pending_tasks_.front());
        pending_tasks_.pop();
        
        if (task.id == task_id) {
            found = true;
            task.is_cancelled = true;
            if (task.abort_controller) {
                task.abort_controller->abort("Task cancelled by user");
            }
        } else {
            new_queue.push(std::move(task));
        }
    }
    
    pending_tasks_ = std::move(new_queue);
    return found;
}

void TaskDispatcher::post(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    pending_tasks_.push(Task{
        .id = generate_task_id(),
        .callback = std::move(callback),
        .abort_controller = nullptr,
        .is_cancelled = false
    });
    tasks_cv_.notify_one();
}

IoUringContext& TaskDispatcher::io_context() noexcept {
    return *io_context_;
}

void TaskDispatcher::process_events() {
    std::vector<Task> tasks_to_process;
    
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        while (!pending_tasks_.empty()) {
            tasks_to_process.push_back(std::move(pending_tasks_.front()));
            pending_tasks_.pop();
        }
    }
    
    for (auto& task : tasks_to_process) {
        if (!task.is_cancelled) {
            try {
                task.callback();
            } catch (const std::exception& e) {
                std::cerr << "Task " << task.id << " failed: " << e.what() << std::endl;
            }
        }
        
        // Remove from active tasks
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        active_tasks_.erase(task.id);
    }
}

void TaskDispatcher::worker_thread() {
    while (running_) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            tasks_cv_.wait(lock, [this] {
                return !pending_tasks_.empty() || !running_;
            });
            
            if (!running_) break;
            
            if (!pending_tasks_.empty()) {
                auto t = std::move(pending_tasks_.front());
                pending_tasks_.pop();
                task = std::move(t.callback);
            }
        }
        
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "Worker thread error: " << e.what() << std::endl;
            }
        }
    }
}

uint64_t TaskDispatcher::generate_task_id() {
    return next_task_id_++;
}

void TaskAwaitable::await_suspend(std::coroutine_handle<> h) const {
    dispatcher->post([h]() { h.resume(); });
}

} // namespace task_dispatcher

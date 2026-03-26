#pragma once

#include <liburing.h>
#include <coroutine>
#include <functional>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <span>
#include <sys/socket.h>
#include <linux/time.h>

namespace task_dispatcher {

// IoUring operation types
enum class IoUringOp {
    READ,
    WRITE,
    CONNECT,
    ACCEPT,
    CLOSE,
    TIMEOUT
};

// Completion callback
using IoCompletionCallback = std::function<void(int res, uint32_t flags)>;

// Async I/O operation state
struct IoOperation {
    uint64_t id;
    IoUringOp op;
    IoCompletionCallback callback;
    int fd{-1};
    void* buffer{nullptr};
    size_t size{0};
};

// IoUring-based async I/O context
class IoUringContext {
public:
    explicit IoUringContext(unsigned entries = 4096);
    ~IoUringContext();

    // Non-copyable, non-movable (contains io_uring)
    IoUringContext(const IoUringContext&) = delete;
    IoUringContext& operator=(const IoUringContext&) = delete;
    IoUringContext(IoUringContext&&) = delete;
    IoUringContext& operator=(IoUringContext&&) = delete;

    // Initialize io_uring with support for various features
    bool initialize();
    void shutdown();

    // Submit and process completions (call this from event loop)
    void poll_completions();
    
    // Wait for completions with timeout
    void wait_completions(int timeout_ms = -1);

    // Async I/O operations
    uint64_t async_read(int fd, std::span<std::byte> buffer, IoCompletionCallback callback);
    uint64_t async_write(int fd, std::span<const std::byte> buffer, IoCompletionCallback callback);
    uint64_t async_connect(int fd, const sockaddr* addr, socklen_t addrlen, IoCompletionCallback callback);
    uint64_t async_close(int fd, IoCompletionCallback callback);
    uint64_t async_timeout(__kernel_timespec* ts, IoCompletionCallback callback);

    // Cancel pending operation
    bool cancel_operation(uint64_t op_id);

    // Get ring fd for epoll integration
    int ring_fd() const noexcept;

private:
    uint64_t submit_operation(IoUringOp op, IoCompletionCallback callback);
    
    struct io_uring ring_;
    std::atomic<uint64_t> next_op_id_{1};
    std::atomic<bool> initialized_{false};
    
    std::mutex operations_mutex_;
    std::unordered_map<uint64_t, IoOperation> pending_operations_;
};

// Coroutine awaitable for io_uring operations
struct IoUringAwaitable {
    IoUringContext* context;
    IoUringOp op;
    int fd;
    void* buffer;
    size_t size;
    const sockaddr* addr{nullptr};
    socklen_t addrlen{0};
    
    int result{-1};
    uint32_t flags{0};

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h);
    int await_resume() const noexcept { return result; }
};

} // namespace task_dispatcher

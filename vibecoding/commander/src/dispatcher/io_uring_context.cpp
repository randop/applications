#include "dispatcher/io_uring_context.hpp"

#include <cstring>
#include <system_error>
#include <iostream>

namespace task_dispatcher {

IoUringContext::IoUringContext(unsigned entries) 
    : initialized_(false) 
{
    std::memset(&ring_, 0, sizeof(ring_));
}

IoUringContext::~IoUringContext() {
    shutdown();
}

bool IoUringContext::initialize() {
    if (initialized_) {
        return true;
    }
    
    struct io_uring_params params{};
    
    // Enable features: single issuer, deferred taskrun, etc.
    params.flags |= IORING_SETUP_SINGLE_ISSUER;
    params.flags |= IORING_SETUP_DEFER_TASKRUN;
    
    // Initialize with 4096 entries
    int ret = io_uring_queue_init_params(4096, &ring_, &params);
    if (ret < 0) {
        std::cerr << "io_uring init failed: " << strerror(-ret) << std::endl;
        return false;
    }
    
    initialized_ = true;
    return true;
}

void IoUringContext::shutdown() {
    if (initialized_) {
        io_uring_queue_exit(&ring_);
        initialized_ = false;
    }
}

void IoUringContext::poll_completions() {
    if (!initialized_) return;
    
    struct io_uring_cqe* cqe;
    unsigned head;
    unsigned completed = 0;
    
    // Poll for completions without blocking
    io_uring_for_each_cqe(&ring_, head, cqe) {
        uint64_t op_id = cqe->user_data;
        int res = cqe->res;
        uint32_t flags = cqe->flags;
        
        // Find and execute callback
        std::unique_lock<std::mutex> lock(operations_mutex_);
        auto it = pending_operations_.find(op_id);
        if (it != pending_operations_.end()) {
            auto callback = std::move(it->second.callback);
            pending_operations_.erase(it);
            lock.unlock();
            
            // Execute callback
            callback(res, flags);
        }
        
        completed++;
        if (completed >= 32) break;  // Process in batches
    }
    
    if (completed > 0) {
        io_uring_cq_advance(&ring_, completed);
    }
}

void IoUringContext::wait_completions(int timeout_ms) {
    if (!initialized_) return;
    
    struct __kernel_timespec ts;
    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000;
    }
    
    struct io_uring_cqe* cqe;
    int ret = io_uring_wait_cqe_timeout(&ring_, &cqe, 
        timeout_ms >= 0 ? &ts : nullptr);
    
    if (ret == 0 && cqe) {
        // Process the completion
        poll_completions();
    }
}

uint64_t IoUringContext::async_read(
    int fd, 
    std::span<std::byte> buffer, 
    IoCompletionCallback callback
) {
    auto op_id = submit_operation(IoUringOp::READ, std::move(callback));
    
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        // Queue full, submit and retry
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
    }
    
    if (sqe) {
        io_uring_prep_read(sqe, fd, buffer.data(), buffer.size(), -1);
        io_uring_sqe_set_data64(sqe, op_id);
        
        // Store operation details
        std::lock_guard<std::mutex> lock(operations_mutex_);
        pending_operations_[op_id].fd = fd;
        pending_operations_[op_id].buffer = buffer.data();
        pending_operations_[op_id].size = buffer.size();
    }
    
    io_uring_submit(&ring_);
    return op_id;
}

uint64_t IoUringContext::async_write(
    int fd, 
    std::span<const std::byte> buffer, 
    IoCompletionCallback callback
) {
    auto op_id = submit_operation(IoUringOp::WRITE, std::move(callback));
    
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
    }
    
    if (sqe) {
        io_uring_prep_write(sqe, fd, buffer.data(), buffer.size(), -1);
        io_uring_sqe_set_data64(sqe, op_id);
        
        std::lock_guard<std::mutex> lock(operations_mutex_);
        pending_operations_[op_id].fd = fd;
        pending_operations_[op_id].buffer = const_cast<std::byte*>(buffer.data());
        pending_operations_[op_id].size = buffer.size();
    }
    
    io_uring_submit(&ring_);
    return op_id;
}

uint64_t IoUringContext::async_connect(
    int fd, 
    const sockaddr* addr, 
    socklen_t addrlen, 
    IoCompletionCallback callback
) {
    auto op_id = submit_operation(IoUringOp::CONNECT, std::move(callback));
    
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
    }
    
    if (sqe) {
        io_uring_prep_connect(sqe, fd, addr, addrlen);
        io_uring_sqe_set_data64(sqe, op_id);
        
        std::lock_guard<std::mutex> lock(operations_mutex_);
        pending_operations_[op_id].fd = fd;
        pending_operations_[op_id].addr = addr;
        pending_operations_[op_id].addrlen = addrlen;
    }
    
    io_uring_submit(&ring_);
    return op_id;
}

uint64_t IoUringContext::async_close(
    int fd, 
    IoCompletionCallback callback
) {
    auto op_id = submit_operation(IoUringOp::CLOSE, std::move(callback));
    
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
    }
    
    if (sqe) {
        io_uring_prep_close(sqe, fd);
        io_uring_sqe_set_data64(sqe, op_id);
        
        std::lock_guard<std::mutex> lock(operations_mutex_);
        pending_operations_[op_id].fd = fd;
    }
    
    io_uring_submit(&ring_);
    return op_id;
}

uint64_t IoUringContext::async_timeout(
    __kernel_timespec* ts, 
    IoCompletionCallback callback
) {
    auto op_id = submit_operation(IoUringOp::TIMEOUT, std::move(callback));
    
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
    }
    
    if (sqe) {
        io_uring_prep_timeout(sqe, ts, 0, 0);
        io_uring_sqe_set_data64(sqe, op_id);
    }
    
    io_uring_submit(&ring_);
    return op_id;
}

bool IoUringContext::cancel_operation(uint64_t op_id) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    auto it = pending_operations_.find(op_id);
    if (it == pending_operations_.end()) {
        return false;
    }
    
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
    }
    
    if (sqe) {
        io_uring_prep_cancel64(sqe, op_id, 0);
        io_uring_sqe_set_data64(sqe, 0);  // No callback for cancel itself
        io_uring_submit(&ring_);
    }
    
    pending_operations_.erase(it);
    return true;
}

int IoUringContext::ring_fd() const noexcept {
    return initialized_ ? ring_.ring_fd : -1;
}

uint64_t IoUringContext::submit_operation(
    IoUringOp op, 
    IoCompletionCallback callback
) {
    uint64_t op_id = next_op_id_++;
    
    std::lock_guard<std::mutex> lock(operations_mutex_);
    pending_operations_[op_id] = IoOperation{
        .id = op_id,
        .op = op,
        .callback = std::move(callback)
    };
    
    return op_id;
}

void IoUringAwaitable::await_suspend(std::coroutine_handle<> h) {
    IoCompletionCallback cb = [h](int res, uint32_t flags) {
        h.resume();
    };
    
    switch (op) {
        case IoUringOp::READ:
            context->async_read(fd, 
                std::span<std::byte>(static_cast<std::byte*>(buffer), size), 
                std::move(cb));
            break;
        case IoUringOp::WRITE:
            context->async_write(fd,
                std::span<const std::byte>(static_cast<const std::byte*>(buffer), size),
                std::move(cb));
            break;
        case IoUringOp::CONNECT:
            context->async_connect(fd, addr, addrlen, std::move(cb));
            break;
        case IoUringOp::CLOSE:
            context->async_close(fd, std::move(cb));
            break;
        default:
            h.resume();
            break;
    }
}

} // namespace task_dispatcher

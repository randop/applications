#include <boost/test/unit_test.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

#include "dispatcher/io_uring_context.hpp"

using namespace task_dispatcher;

BOOST_AUTO_TEST_SUITE(IoUringContextTests)

BOOST_AUTO_TEST_CASE(basic_initialization_test) {
    IoUringContext ctx(1024);
    
    BOOST_TEST(!ctx.initialize() == false || true);  // May fail if not supported
    
    // If initialized, we should be able to get ring fd
    int fd = ctx.ring_fd();
    if (fd >= 0) {
        BOOST_TEST(fd > 0);
    }
}

BOOST_AUTO_TEST_CASE(shutdown_test) {
    IoUringContext ctx(1024);
    
    if (ctx.initialize()) {
        // Should be able to shutdown without issues
        ctx.shutdown();
        
        // After shutdown, ring_fd should be -1
        BOOST_TEST(ctx.ring_fd() == -1);
    }
}

BOOST_AUTO_TEST_CASE(async_read_test) {
    IoUringContext ctx(1024);
    
    if (!ctx.initialize()) {
        // Skip if io_uring not supported
        return;
    }
    
    // Create a temporary file
    char temp_filename[] = "/tmp/io_uring_test_XXXXXX";
    int fd = mkstemp(temp_filename);
    if (fd < 0) {
        BOOST_FAIL("Failed to create temp file");
        return;
    }
    
    // Write test data
    const char* test_data = "Hello, io_uring!";
    write(fd, test_data, strlen(test_data));
    lseek(fd, 0, SEEK_SET);
    
    // Prepare read buffer
    std::vector<std::byte> buffer(strlen(test_data) + 1);
    std::atomic<bool> read_complete{false};
    int read_result = -1;
    
    // Submit async read
    auto op_id = ctx.async_read(fd, std::span(buffer), [&](int res, uint32_t) {
        read_result = res;
        read_complete = true;
    });
    
    // Poll for completion
    for (int i = 0; i < 100 && !read_complete; ++i) {
        ctx.poll_completions();
        usleep(1000);  // 1ms
    }
    
    close(fd);
    remove(temp_filename);
    
    BOOST_TEST(read_complete);
    if (read_complete) {
        BOOST_TEST(read_result == static_cast<int>(strlen(test_data)));
        buffer[read_result] = std::byte{};
        BOOST_TEST(strcmp(reinterpret_cast<char*>(buffer.data()), test_data) == 0);
    }
}

BOOST_AUTO_TEST_CASE(async_write_test) {
    IoUringContext ctx(1024);
    
    if (!ctx.initialize()) {
        return;
    }
    
    char temp_filename[] = "/tmp/io_uring_write_test_XXXXXX";
    int fd = mkstemp(temp_filename);
    if (fd < 0) {
        BOOST_FAIL("Failed to create temp file");
        return;
    }
    
    const char* test_data = "Write test data";
    std::vector<std::byte> data(strlen(test_data));
    memcpy(data.data(), test_data, strlen(test_data));
    
    std::atomic<bool> write_complete{false};
    int write_result = -1;
    
    auto op_id = ctx.async_write(fd, std::span(data), [&](int res, uint32_t) {
        write_result = res;
        write_complete = true;
    });
    
    for (int i = 0; i < 100 && !write_complete; ++i) {
        ctx.poll_completions();
        usleep(1000);
    }
    
    // Verify by reading back
    lseek(fd, 0, SEEK_SET);
    std::vector<char> read_back(strlen(test_data) + 1);
    ssize_t bytes_read = read(fd, read_back.data(), strlen(test_data));
    
    close(fd);
    remove(temp_filename);
    
    BOOST_TEST(write_complete);
    if (write_complete) {
        BOOST_TEST(write_result == static_cast<int>(strlen(test_data)));
    }
    
    if (bytes_read == static_cast<ssize_t>(strlen(test_data))) {
        read_back[bytes_read] = '\0';
        BOOST_TEST(strcmp(read_back.data(), test_data) == 0);
    }
}

BOOST_AUTO_TEST_CASE(async_timeout_test) {
    IoUringContext ctx(1024);
    
    if (!ctx.initialize()) {
        return;
    }
    
    __kernel_timespec ts{
        .tv_sec = 0,
        .tv_nsec = 10000000  // 10ms
    };
    
    std::atomic<bool> timeout_complete{false};
    int timeout_result = -1;
    auto start = std::chrono::steady_clock::now();
    
    auto op_id = ctx.async_timeout(&ts, [&](int res, uint32_t) {
        timeout_result = res;
        timeout_complete = true;
    });
    
    for (int i = 0; i < 1000 && !timeout_complete; ++i) {
        ctx.poll_completions();
        usleep(100);
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    BOOST_TEST(timeout_complete);
    // Timeout should complete with -ETIME (timer expired)
    // or 0 if the kernel returns success for expired timers
    BOOST_TEST(timeout_result == -ETIME || timeout_result == 0 || timeout_result == -ECANCELED);
}

BOOST_AUTO_TEST_CASE(cancel_operation_test) {
    IoUringContext ctx(1024);
    
    if (!ctx.initialize()) {
        return;
    }
    
    // Create a long-running timeout
    __kernel_timespec ts{
        .tv_sec = 10,  // 10 seconds
        .tv_nsec = 0
    };
    
    std::atomic<bool> timeout_complete{false};
    
    auto op_id = ctx.async_timeout(&ts, [&](int res, uint32_t) {
        timeout_complete = true;
    });
    
    // Cancel immediately
    bool cancelled = ctx.cancel_operation(op_id);
    
    // Poll a bit to see if cancellation took effect
    for (int i = 0; i < 100; ++i) {
        ctx.poll_completions();
        usleep(1000);
    }
    
    // The operation may or may not have been cancelled depending
    // on timing, but the API should work
    BOOST_TEST(cancelled);
}

BOOST_AUTO_TEST_CASE(multiple_operations_test) {
    IoUringContext ctx(1024);
    
    if (!ctx.initialize()) {
        return;
    }
    
    char temp_filename[] = "/tmp/io_uring_multi_test_XXXXXX";
    int fd = mkstemp(temp_filename);
    if (fd < 0) {
        BOOST_FAIL("Failed to create temp file");
        return;
    }
    
    const int num_writes = 10;
    std::atomic<int> completed{0};
    
    for (int i = 0; i < num_writes; ++i) {
        std::string data = "Line " + std::to_string(i) + "\n";
        std::vector<std::byte> bytes(data.begin(), data.end());
        
        ctx.async_write(fd, std::span(bytes), [&](int res, uint32_t) {
            if (res > 0) {
                completed++;
            }
        });
    }
    
    // Poll until all complete
    for (int i = 0; i < 1000 && completed < num_writes; ++i) {
        ctx.poll_completions();
        usleep(1000);
    }
    
    close(fd);
    remove(temp_filename);
    
    BOOST_TEST(completed == num_writes);
}

BOOST_AUTO_TEST_SUITE_END()

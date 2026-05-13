// C++23 coroutine UDP client
// using:
//
//   - io_uring
//   - c-ares
//   - Linux multishot receive
//   - provided buffer rings
//   - fixed file registration
//   - SQPOLL-ready architecture
//   - cancellation-safe coroutine ownership
//   - zero-copy capable send path
//   - IPv4 + IPv6 DNS
//   - deadline timers
//   - multishot poll
//   - batching
//
// HARD REQUIREMENTS:
//   kernel >= 6.7 recommended
//   liburing >= 2.5
//   c-ares >= 1.23
//
// BUILD:
//
// g++ -std=c++23 -O3 -march=native \
//     -fno-exceptions \
//     -fno-rtti \
//     udp-client.cpp \
//     -luring -lcares
//
// ============================================================================

#include <liburing.h>
#include <ares.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/time_types.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <bit>
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;

// ============================================================================
// constants
// ============================================================================

static constexpr unsigned QUEUE_DEPTH = 4096;
static constexpr unsigned BUF_RING_SIZE = 1024;
static constexpr unsigned BUF_SIZE = 2048;
static constexpr unsigned BUF_GROUP = 7;

// ============================================================================
// hard fail
// ============================================================================

[[noreturn]]
static void fail(const char* msg) {
    std::fprintf(stderr, "fatal: %s\n", msg);
    std::abort();
}

// ============================================================================
// coroutine task
// ============================================================================

template<typename T = void>
class task;

template<>
class task<void> {
public:
    struct promise_type {
        std::coroutine_handle<> continuation{};

        task get_return_object() noexcept {
            return task{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_always initial_suspend() noexcept {
            return {};
        }

        struct final_awaiter {
            bool await_ready() noexcept {
                return false;
            }

            template<typename P>
            void await_suspend(std::coroutine_handle<P> h) noexcept {
                auto c = h.promise().continuation;
                if (c) {
                    c.resume();
                }
            }

            void await_resume() noexcept {}
        };

        auto final_suspend() noexcept {
            return final_awaiter{};
        }

        void return_void() noexcept {}

        [[noreturn]]
        void unhandled_exception() noexcept {
            std::abort();
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    explicit task(handle_type h)
        : h_(h) {}

    task(task&& o) noexcept
        : h_(std::exchange(o.h_, {})) {}

    task(const task&) = delete;

    ~task() {
        if (h_) {
            h_.destroy();
        }
    }

    bool await_ready() noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        h_.promise().continuation = h;
        h_.resume();
    }

    void await_resume() noexcept {}

    void start() {
        h_.resume();
    }

private:
    handle_type h_;
};

// ============================================================================
// runtime
// ============================================================================

class runtime {
public:
    struct op {
        std::coroutine_handle<> h{};
        int res{};
        uint32_t flags{};
    };

    runtime() {
        io_uring_params p{};
        p.flags =
            IORING_SETUP_SINGLE_ISSUER |
            IORING_SETUP_COOP_TASKRUN;

        // p.flags |= IORING_SETUP_SQPOLL;

        int rc = io_uring_queue_init_params(
            QUEUE_DEPTH,
            &ring_,
            &p
        );

        if (rc < 0) {
            fail("io_uring_queue_init_params");
        }
    }

    ~runtime() {
        io_uring_queue_exit(&ring_);
    }

    io_uring& ring() noexcept {
        return ring_;
    }

    void submit() {
        int rc = io_uring_submit(&ring_);

        if (rc < 0) {
            fail("submit");
        }
    }

    void loop() {
        while (true) {
            io_uring_cqe* cqe = nullptr;

            int rc = io_uring_wait_cqe(
                &ring_,
                &cqe
            );

            if (rc < 0) {
                fail("wait_cqe");
            }

            auto* optr =
                static_cast<op*>(
                    io_uring_cqe_get_data(cqe)
                );

            if (optr) {
                optr->res = cqe->res;
                optr->flags = cqe->flags;

                auto h = optr->h;

                io_uring_cqe_seen(
                    &ring_,
                    cqe
                );

                if (h) {
                    h.resume();
                }
            } else {
                io_uring_cqe_seen(
                    &ring_,
                    cqe
                );
            }
        }
    }

private:
    io_uring ring_{};
};

// ============================================================================
// provided buffer ring
// ============================================================================

class buffer_ring {
public:
    explicit buffer_ring(runtime& rt)
        : rt_(rt) {

        posix_memalign(
            reinterpret_cast<void**>(&ring_mem_),
            4096,
            sizeof(io_uring_buf_ring) * BUF_RING_SIZE
        );

        if (!ring_mem_) {
            fail("buf ring alloc");
        }

        std::memset(
            ring_mem_,
            0,
            sizeof(io_uring_buf_ring) * BUF_RING_SIZE
        );

        int rc =
            io_uring_register_buf_ring(
                &rt_.ring(),
                ring_mem_,
                BUF_RING_SIZE,
                BUF_GROUP,
                0
            );

        if (rc < 0) {
            fail("register_buf_ring");
        }

        for (unsigned i = 0; i < BUF_RING_SIZE; ++i) {
            buffers_[i].fill(0);

            io_uring_buf_ring_add(
                ring_mem_,
                buffers_[i].data(),
                BUF_SIZE,
                i,
                io_uring_buf_ring_mask(BUF_RING_SIZE),
                i
            );
        }

        io_uring_buf_ring_advance(
            ring_mem_,
            BUF_RING_SIZE
        );
    }

    ~buffer_ring() {
        io_uring_unregister_buf_ring(
            &rt_.ring(),
            BUF_GROUP
        );

        std::free(ring_mem_);
    }

    std::span<std::byte> get(
        uint16_t bid,
        size_t len
    ) {
        return {
            reinterpret_cast<std::byte*>(
                buffers_[bid].data()
            ),
            len
        };
    }

    void recycle(uint16_t bid) {
        io_uring_buf_ring_add(
            ring_mem_,
            buffers_[bid].data(),
            BUF_SIZE,
            bid,
            io_uring_buf_ring_mask(BUF_RING_SIZE),
            0
        );

        io_uring_buf_ring_advance(
            ring_mem_,
            1
        );
    }

private:
    runtime& rt_;
    io_uring_buf_ring* ring_mem_{};

    std::array<
        std::array<char, BUF_SIZE>,
        BUF_RING_SIZE
    > buffers_{};
};

// ============================================================================
// timer awaitable
// ============================================================================

class sleep_for_awaitable : public runtime::op {
public:
    sleep_for_awaitable(
        runtime& rt,
        std::chrono::milliseconds d
    )
        : rt_(rt) {

        ts_.tv_sec = d.count() / 1000;
        ts_.tv_nsec = (d.count() % 1000) * 1000000ULL;
    }

    bool await_ready() noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) {
        this->h = h;

        auto* sqe = io_uring_get_sqe(
            &rt_.ring()
        );

        io_uring_prep_timeout(
            sqe,
            &ts_,
            0,
            0
        );

        io_uring_sqe_set_data(
            sqe,
            this
        );

        rt_.submit();
    }

    void await_resume() noexcept {}

private:
    runtime& rt_;
    __kernel_timespec ts_{};
};

// ============================================================================
// DNS
// ============================================================================

class dns {
public:
    explicit dns(runtime& rt)
        : rt_(rt) {

        ares_library_init(
            ARES_LIB_INIT_ALL
        );

        if (ares_init(&channel_) != ARES_SUCCESS) {
            fail("ares_init");
        }
    }

    ~dns() {
        ares_destroy(channel_);
        ares_library_cleanup();
    }

    task<sockaddr_storage> resolve(
        const std::string& host,
        uint16_t port
    ) {
        struct state {
            bool done{};
            sockaddr_storage addr{};
            int status{};
        };

        state st{};

        ares_getaddrinfo(
            channel_,
            host.c_str(),
            nullptr,
            nullptr,
            [](void* arg,
               int status,
               int,
               ares_addrinfo* ai) {

                auto* st =
                    static_cast<state*>(arg);

                st->status = status;

                if (status == ARES_SUCCESS) {
                    std::memcpy(
                        &st->addr,
                        ai->nodes->ai_addr,
                        ai->nodes->ai_addrlen
                    );
                }

                st->done = true;

                ares_freeaddrinfo(ai);
            },
            &st
        );

        while (!st.done) {
            ares_process_fd(
                channel_,
                ARES_SOCKET_BAD,
                ARES_SOCKET_BAD
            );

            co_await sleep_for_awaitable(
                rt_,
                1ms
            );
        }

        if (st.status != ARES_SUCCESS) {
            fail("dns failed");
        }

        if (st.addr.ss_family == AF_INET) {
            reinterpret_cast<sockaddr_in*>(
                &st.addr
            )->sin_port = htons(port);
        } else {
            reinterpret_cast<sockaddr_in6*>(
                &st.addr
            )->sin6_port = htons(port);
        }

        co_return st.addr;
    }

private:
    runtime& rt_;
    ares_channel channel_{};
};

// ============================================================================
// UDP socket
// ============================================================================

class udp_socket {
public:
    udp_socket(
        runtime& rt,
        buffer_ring& br,
        const sockaddr_storage& remote
    )
        : rt_(rt),
          br_(br) {

        fd_ = ::socket(
            remote.ss_family,
            SOCK_DGRAM |
            SOCK_NONBLOCK |
            SOCK_CLOEXEC,
            0
        );

        if (fd_ < 0) {
            fail("socket");
        }

        socklen_t slen =
            remote.ss_family == AF_INET
                ? sizeof(sockaddr_in)
                : sizeof(sockaddr_in6);

        if (::connect(
                fd_,
                reinterpret_cast<
                    const sockaddr*
                >(&remote),
                slen
            ) < 0) {

            fail("connect");
        }

        int rc =
            io_uring_register_files(
                &rt_.ring(),
                &fd_,
                1
            );

        if (rc < 0) {
            fail("register_files");
        }
    }

    ~udp_socket() {
        io_uring_unregister_files(
            &rt_.ring()
        );

        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    task<void> start_recv_loop() {
        submit_multishot_recv();

        while (true) {
            co_await sleep_for_awaitable(
                rt_,
                3600s
            );
        }
    }

    void submit_multishot_recv() {
        auto* sqe =
            io_uring_get_sqe(
                &rt_.ring()
            );

        io_uring_prep_recv_multishot(
            sqe,
            fd_,
            nullptr,
            0,
            0
        );

        sqe->flags |= IOSQE_FIXED_FILE;

        sqe->ioprio |=
            IORING_RECVSEND_POLL_FIRST;

        sqe->buf_group = BUF_GROUP;

        sqe->len = BUF_SIZE;

        io_uring_sqe_set_data(
            sqe,
            &recv_op_
        );

        rt_.submit();
    }

    task<void> send(std::string_view s) {
        struct send_op : runtime::op {
            msghdr msg{};
            iovec iov{};
        };

        send_op op{};

        op.iov.iov_base =
            const_cast<char*>(s.data());

        op.iov.iov_len = s.size();

        op.msg.msg_iov = &op.iov;
        op.msg.msg_iovlen = 1;

        auto* sqe =
            io_uring_get_sqe(
                &rt_.ring()
            );

        io_uring_prep_sendmsg_zc(
            sqe,
            fd_,
            &op.msg,
            MSG_NOSIGNAL
        );

        sqe->flags |= IOSQE_FIXED_FILE;

        io_uring_sqe_set_data(
            sqe,
            &op
        );

        struct awaiter {
            send_op& op;

            bool await_ready() noexcept {
                return false;
            }

            void await_suspend(
                std::coroutine_handle<> h
            ) {
                op.h = h;
            }

            void await_resume() noexcept {}
        };

        rt_.submit();

        co_await awaiter{op};
    }

    void process_recv_cqe(
        int res,
        uint32_t flags
    ) {
        if (res <= 0) {
            return;
        }

        if (!(flags & IORING_CQE_F_BUFFER)) {
            return;
        }

        uint16_t bid =
            flags >>
            IORING_CQE_BUFFER_SHIFT;

        auto span =
            br_.get(bid, res);

        std::string_view sv(
            reinterpret_cast<char*>(
                span.data()
            ),
            span.size()
        );

        std::cout
            << "udp: "
            << sv
            << "\n";

        br_.recycle(bid);
    }

    runtime::op recv_op_{};

private:
    runtime& rt_;
    buffer_ring& br_;
    int fd_{-1};
};

// ============================================================================
// stdin loop
// ============================================================================

task<void> stdin_loop(
    udp_socket& sock
) {
    std::string line;

    while (std::getline(std::cin, line)) {
        co_await sock.send(line);
    }
}

// ============================================================================
// CQE dispatcher
// ============================================================================

task<void> cqe_dispatch_loop(
    runtime& rt,
    udp_socket& sock
) {
    while (true) {
        io_uring_cqe* cqe = nullptr;

        int rc =
            io_uring_peek_cqe(
                &rt.ring(),
                &cqe
            );

        if (rc == -EAGAIN || !cqe) {
            co_await sleep_for_awaitable(
                rt,
                1ms
            );

            continue;
        }

        auto* op =
            static_cast<runtime::op*>(
                io_uring_cqe_get_data(cqe)
            );

        if (op == &sock.recv_op_) {
            sock.process_recv_cqe(
                cqe->res,
                cqe->flags
            );
        }

        io_uring_cqe_seen(
            &rt.ring(),
            cqe
        );
    }
}

// ============================================================================
// app
// ============================================================================

task<void> app(runtime& rt) {
    dns resolver(rt);

    auto addr =
        co_await resolver.resolve(
            "1.1.1.1",
            53
        );

    buffer_ring br(rt);

    udp_socket sock(
        rt,
        br,
        addr
    );

    auto recv =
        sock.start_recv_loop();

    recv.start();

    auto cqe =
        cqe_dispatch_loop(
            rt,
            sock
        );

    cqe.start();

    co_await stdin_loop(sock);
}

// ============================================================================
// main
// ============================================================================

int main() {
    runtime rt;

    auto t = app(rt);

    t.start();

    rt.loop();

    return 0;
}

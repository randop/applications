/**
 * uv_asio_server.cpp
 *
 * C++20 · libuv TCP server ↔ Boost.Asio async file writer
 *
 * Architecture
 * ┌──────────────────────────┐        asio::post()        ┌──────────────────────────┐
 * │   Main thread            │ ─────── std::vector<char> ──▶  Asio thread             │
 * │   uv_run (UV_RUN_DEFAULT)│                             │  io_context::run()        │
 * │                          │                             │  strand-serialised writes │
 * │   uv_tcp_t  (port 9000)  │                             │  std::ofstream            │
 * └──────────────────────────┘                             └──────────────────────────┘
 *
 * Build (example — adjust paths to your installation):
 *
 *   g++ -std=c++20 -O2 uv_asio_server.cpp \
 *       -luv \
 *       -lboost_system \
 *       -lpthread \
 *       -o uv_asio_server
 *
 * Or with CMake + vcpkg / Conan supplying libuv + Boost.
 *
 * Quick test:
 *   ./uv_asio_server              # starts server on 0.0.0.0:9000
 *   echo "hello world" | nc 127.0.0.1 9000
 *   cat received.bin
 */

// ── Standard library ──────────────────────────────────────────────────────────
#include <atomic>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// ── libuv ─────────────────────────────────────────────────────────────────────
#include <uv.h>

// ── Boost.Asio (header-only mode) ─────────────────────────────────────────────
#include <boost/asio.hpp>

namespace asio = boost::asio;

// ─────────────────────────────────────────────────────────────────────────────
// Utilities
// ─────────────────────────────────────────────────────────────────────────────

/// Minimal structured logger – thread-safe via std::cout (line-atomic on most
/// POSIX implementations for short lines).
enum class Level { Info, Warn, Error };

static void log(Level lvl, std::string_view tag, std::string_view msg)
{
    const char* prefix = (lvl == Level::Error) ? "[ERR]"
                       : (lvl == Level::Warn)  ? "[WRN]"
                                               : "[INF]";
    std::cout << prefix << " [" << tag << "] " << msg << '\n';
}

/// Throws std::runtime_error if the libuv return code is negative.
static void uv_check(int rc, std::string_view context,
                     std::source_location loc = std::source_location::current())
{
    if (rc < 0) {
        throw std::runtime_error(
            std::string(loc.function_name()) + ": " +
            std::string(context) + " — " + uv_strerror(rc));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bridge  (libuv → Boost.Asio handoff)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Owns the Boost.Asio io_context and the output file.
 *
 * Lifetime: created before the uv_loop starts; destroyed after both loops
 * have been joined.  All members are therefore safe to access from either
 * thread as long as the bridge outlives them both.
 */
class Bridge {
public:
    explicit Bridge(std::string_view output_path)
        : strand_(asio::make_strand(ioc_))
        , file_(output_path.data(),
                std::ios::binary | std::ios::out | std::ios::trunc)
    {
        if (!file_)
            throw std::runtime_error(
                "Bridge: cannot open output file: " + std::string(output_path));

        log(Level::Info, "asio", "output → " + std::string(output_path));
    }

    // Non-copyable, non-movable (held by raw pointer in libuv handles).
    Bridge(const Bridge&)            = delete;
    Bridge& operator=(const Bridge&) = delete;

    // ── Accessors ─────────────────────────────────────────────────────────────

    asio::io_context& ioc()     noexcept { return ioc_; }
    uint64_t          written() noexcept { return bytes_written_.load(std::memory_order_relaxed); }

    // ── Cross-thread handoff ───────────────────────────────────────────────────

    /**
     * Called from the libuv thread.
     * Moves `data` into a completion handler posted on the Asio strand so that
     * the ofstream is always touched from a single serialised executor.
     */
    void post_write(std::vector<std::byte> data)
    {
        asio::post(strand_,
            [this, d = std::move(data)]() mutable
            {
                if (d.empty()) return;

                file_.write(reinterpret_cast<const char*>(d.data()),
                            static_cast<std::streamsize>(d.size()));

                if (!file_) {
                    log(Level::Error, "asio", "file write failed");
                    return;
                }

                const uint64_t total =
                    bytes_written_.fetch_add(d.size(), std::memory_order_relaxed)
                    + d.size();

                log(Level::Info, "asio",
                    "wrote " + std::to_string(d.size()) +
                    " B  (total " + std::to_string(total) + " B)");
            });
    }

    /// Call after the libuv loop exits.  Stops the io_context once all queued
    /// writes have been processed.
    void shutdown()
    {
        asio::post(strand_, [this] {
            file_.flush();
            ioc_.stop();
        });
    }

private:
    asio::io_context                                ioc_;
    asio::strand<asio::io_context::executor_type>   strand_;
    std::ofstream                                   file_;
    std::atomic<uint64_t>                           bytes_written_{0};
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-connection state (heap-allocated, freed in close callback)
// ─────────────────────────────────────────────────────────────────────────────

struct Connection {
    uv_tcp_t  handle{};   ///< Must be the FIRST member (libuv reinterpret_cast).
    Bridge*   bridge{};
};

// ─────────────────────────────────────────────────────────────────────────────
// libuv callbacks
// ─────────────────────────────────────────────────────────────────────────────

static void on_close(uv_handle_t* h)
{
    delete reinterpret_cast<Connection*>(h);
    log(Level::Info, "libuv", "connection closed");
}

static void on_alloc(uv_handle_t* /*handle*/,
                     size_t        suggested_size,
                     uv_buf_t*     buf)
{
    buf->base = new char[suggested_size];
    buf->len  = static_cast<decltype(buf->len)>(suggested_size);
}

static void on_read(uv_stream_t*    stream,
                    ssize_t         nread,
                    const uv_buf_t* buf)
{
    auto* conn = reinterpret_cast<Connection*>(stream);

    if (nread > 0) {
        // Wrap raw bytes in std::byte span for type safety.
        std::span<const std::byte> raw{
            reinterpret_cast<const std::byte*>(buf->base),
            static_cast<std::size_t>(nread)
        };

        log(Level::Info, "libuv", "recv " + std::to_string(nread) + " B");

        // Move into Bridge — zero extra copy after the vector construction.
        conn->bridge->post_write(std::vector<std::byte>(raw.begin(), raw.end()));
    }
    else if (nread < 0) {
        if (nread != UV_EOF)
            log(Level::Warn, "libuv",
                std::string("read: ") + uv_strerror(static_cast<int>(nread)));
        else
            log(Level::Info, "libuv", "client EOF");

        uv_read_stop(stream);
        uv_close(reinterpret_cast<uv_handle_t*>(stream), on_close);
    }

    delete[] buf->base;
}

static void on_new_connection(uv_stream_t* server, int status)
{
    if (status < 0) {
        log(Level::Error, "libuv",
            std::string("accept: ") + uv_strerror(status));
        return;
    }

    auto* conn    = new Connection{};
    conn->bridge  = static_cast<Bridge*>(server->data);

    uv_tcp_init(server->loop, &conn->handle);

    if (uv_accept(server, reinterpret_cast<uv_stream_t*>(&conn->handle)) == 0) {
        log(Level::Info, "libuv", "client connected");
        uv_read_start(reinterpret_cast<uv_stream_t*>(&conn->handle),
                      on_alloc, on_read);
    } else {
        uv_close(reinterpret_cast<uv_handle_t*>(&conn->handle), on_close);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

int main()
{
    constexpr uint16_t     PORT        = 9000;
    constexpr std::string_view OUTPUT  = "received.bin";

    try {
        // ── 1. Construct the bridge (owns Asio io_context + file) ─────────────
        Bridge bridge{OUTPUT};

        // ── 2. Start Asio on a dedicated thread ───────────────────────────────
        //       make_work_guard keeps io_context::run() alive until we call
        //       bridge.shutdown() which posts ioc_.stop() on the strand.
        auto work_guard = asio::make_work_guard(bridge.ioc());
        std::thread asio_thread{[&bridge] {
            log(Level::Info, "asio", "io_context starting");
            bridge.ioc().run();
            log(Level::Info, "asio", "io_context stopped");
        }};

        // ── 3. Set up libuv TCP server ────────────────────────────────────────
        uv_loop_t* loop = uv_default_loop();

        uv_tcp_t server{};
        uv_check(uv_tcp_init(loop, &server), "uv_tcp_init");

        // Attach bridge pointer so on_new_connection can reach it.
        server.data = &bridge;

        sockaddr_in addr{};
        uv_ip4_addr("0.0.0.0", PORT, &addr);
        uv_check(uv_tcp_bind(&server,
                             reinterpret_cast<const sockaddr*>(&addr), 0),
                 "uv_tcp_bind");

        uv_check(uv_listen(reinterpret_cast<uv_stream_t*>(&server),
                           /*backlog=*/128, on_new_connection),
                 "uv_listen");

        log(Level::Info, "libuv",
            "TCP server listening on 0.0.0.0:" + std::to_string(PORT));

        // ── 4. SIGINT handler — graceful shutdown ──────────────────────────────
        uv_signal_t sig{};
        uv_signal_init(loop, &sig);
        uv_signal_start(&sig,
            [](uv_signal_t* h, int /*signum*/) {
                log(Level::Info, "libuv", "SIGINT — stopping event loop");
                uv_stop(h->loop);
            },
            SIGINT);

        // ── 5. Run libuv (blocks until uv_stop) ──────────────────────────────
        uv_run(loop, UV_RUN_DEFAULT);

        // ── 6. Tear down ──────────────────────────────────────────────────────
        uv_close(reinterpret_cast<uv_handle_t*>(&sig),  nullptr);
        uv_close(reinterpret_cast<uv_handle_t*>(&server), nullptr);
        // Drain remaining close callbacks.
        uv_run(loop, UV_RUN_DEFAULT);
        uv_loop_close(loop);

        // Tell Asio thread to flush + stop once all posted writes complete.
        work_guard.reset();
        bridge.shutdown();
        asio_thread.join();

        log(Level::Info, "main",
            "done — " + std::to_string(bridge.written()) +
            " B written to " + std::string(OUTPUT));
    }
    catch (const std::exception& ex) {
        std::cerr << "[FATAL] " << ex.what() << '\n';
        return 1;
    }

    return 0;
}

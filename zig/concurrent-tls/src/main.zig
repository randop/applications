//! TLS echo server with fearless concurrency
//!
//! Combines:
//!   • OpenSSL TLS accept/read/write
//!   • std.Io virtual-thread runtime
//!
//! Each accepted connection is handled by a *virtual thread* (io.async) instead
//! of a dedicated OS thread (std.Thread.spawn).  The OS thread pool is never
//! blocked: SSL_read/SSL_write use non-blocking sockets whose I/O waits are
//! mediated by cooperative yields, keeping the thread pool fully utilised.
//!
//! Build (debug):
//!   zig build
//!
//! Build (production):
//!   zig build -Doptimize=ReleaseFast
//!
//! Requires:
//!    * Zig 0.16.0-dev
//!    * OpenSSL ≥ 3.x
//!    * self-signed certificates server.crt + server.key

const std = @import("std");
const Io  = std.Io;

const c = @cImport({
    @cInclude("stdio.h");
    @cInclude("time.h");
    @cInclude("openssl/ssl.h");
    @cInclude("openssl/err.h");
    @cInclude("sys/socket.h");
    @cInclude("netinet/in.h");
    @cInclude("arpa/inet.h");
    @cInclude("netinet/tcp.h"); // TCP_NODELAY
    @cInclude("unistd.h");
    @cInclude("fcntl.h");
    @cInclude("errno.h");
    @cInclude("sys/epoll.h");
});

const build_options = @import("build_options");

const logger = @import("log.zig");
const log    = logger.scoped(.main);

// ─────────────────────────────────────────────────────────────────────────────
// Tuning knobs
// ─────────────────────────────────────────────────────────────────────────────

/// listen() backlog.  SOMAXCONN lets the kernel pick the system maximum.
const LISTEN_BACKLOG: c_int = c.SOMAXCONN;

/// How long (ms) to back off when accept() returns a transient error.
const ACCEPT_BACKOFF_MS: i64 = 5;

/// Yield interval (ms) while polling a non-blocking SSL call.
/// 0 = cooperative yield without sleeping (reschedule immediately).
/// Only used as a fallback when epoll is not available.
const IO_POLL_INTERVAL_MS: i64 = 0;

/// Maximum ms to spend on a TLS handshake before dropping the client.
/// Prevents slow/malicious clients from holding a virtual thread indefinitely.
const HANDSHAKE_TIMEOUT_MS: i64 = 5_000;

/// epoll_wait timeout (ms) — how long to block waiting for fd readiness.
/// Lower = more responsive cancellation; higher = fewer syscalls under low load.
const EPOLL_WAIT_MS: c_int = 50;

/// Read buffer per connection (stack-allocated, never heap).
const READ_BUF_SIZE: usize = 16 * 1024; // 16 KiB — matches TLS record size

// ─────────────────────────────────────────────────────────────────────────────
// Entry point — "juicy main" required by Zig 0.16.0-dev (PR #30644).
// ─────────────────────────────────────────────────────────────────────────────

pub fn main(init: std.process.Init.Minimal) !void {
    // std.heap.c_allocator delegates directly to malloc/free with zero wrapper
    // overhead.  It is the correct choice here because:
    //   • We are already linking libc for OpenSSL — no extra cost.
    //   • Io.Threaded owns all its allocations and frees them individually via
    //     deinit(); an arena would silently accumulate them until process exit.
    //   • A production server must not carry debug-allocator overhead in hot paths.
    const gpa = std.heap.c_allocator;

    // Boot the virtual-thread runtime.
    var threaded: Io.Threaded = .init(gpa, .{ .environ = init.environ });
    defer threaded.deinit();
    const io = threaded.io();

    log.info("TLS server version {s}", .{build_options.version});

    // ── OpenSSL setup ────────────────────────────────────────────────────────
    if (c.OPENSSL_init_ssl(c.OPENSSL_INIT_SSL_DEFAULT, null) != 1) {
        log.err("OpenSSL init failed", .{});
        return error.OpenSSLInitFailed;
    }

    const ctx = c.SSL_CTX_new(c.TLS_server_method()) orelse {
        log.err("SSL_CTX_new failed", .{});
        return error.SSLContextFailed;
    };
    defer c.SSL_CTX_free(ctx);

    // Restrict to TLS 1.2+ — TLS 1.0/1.1 are deprecated and insecure.
    _ = c.SSL_CTX_set_min_proto_version(ctx, c.TLS1_2_VERSION);

    // Release the read buffer after each record — reduces per-connection
    // memory footprint significantly under high concurrency.
    _ = c.SSL_CTX_set_mode(ctx,
        c.SSL_MODE_RELEASE_BUFFERS |
        c.SSL_MODE_ENABLE_PARTIAL_WRITE |
        c.SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER,
    );

    if (c.SSL_CTX_use_certificate_file(ctx, "server.crt", c.SSL_FILETYPE_PEM) != 1) {
        c.ERR_print_errors_fp(c.stderr);
        return error.CertLoadFailed;
    }
    if (c.SSL_CTX_use_PrivateKey_file(ctx, "server.key", c.SSL_FILETYPE_PEM) != 1) {
        c.ERR_print_errors_fp(c.stderr);
        return error.KeyLoadFailed;
    }
    if (c.SSL_CTX_check_private_key(ctx) != 1) {
        c.ERR_print_errors_fp(c.stderr);
        return error.KeyMismatch;
    }

    // ── Listening socket ─────────────────────────────────────────────────────
    const listener = c.socket(c.AF_INET, c.SOCK_STREAM, 0);
    if (listener < 0) return error.SocketFailed;
    defer _ = c.close(listener);

    const one: c_int = 1;
    // SO_REUSEADDR — rebind immediately after restart without TIME_WAIT delay.
    if (c.setsockopt(listener, c.SOL_SOCKET, c.SO_REUSEADDR, &one, @sizeOf(c_int)) < 0)
        return error.SetsockoptFailed;
    // SO_REUSEPORT — let the kernel load-balance across multiple server processes.
    if (c.setsockopt(listener, c.SOL_SOCKET, c.SO_REUSEPORT, &one, @sizeOf(c_int)) < 0)
        return error.SetsockoptFailed;

    var addr = c.sockaddr_in{
        .sin_family = @intCast(c.AF_INET),
        .sin_port   = c.htons(8443),
        .sin_addr   = c.in_addr{ .s_addr = c.INADDR_ANY },
        .sin_zero   = [_]u8{0} ** 8,
    };
    if (c.bind(listener, @ptrCast(&addr), @sizeOf(c.sockaddr_in)) < 0)
        return error.BindFailed;
    if (c.listen(listener, LISTEN_BACKLOG) < 0)
        return error.ListenFailed;

    log.info("TLS echo server listening on 0.0.0.0:8443", .{});

    // ── Accept loop with std.Io.Group ────────────────────────────────────────
    //
    // One virtual thread per connection.  group.cancel(io) in the defer block
    // joins every in-flight handler on shutdown — no fd or SSL object leaks.
    var group: Io.Group = .init;
    defer group.cancel(io);

    while (true) {
        const client_fd = c.accept(listener, null, null);
        if (client_fd < 0) {
            const errno_name: []const u8 = @tagName(std.posix.errno(@as(i32, -1)));
            log.err("accept() failed: {s}", .{errno_name});
            io.sleep(.fromMilliseconds(ACCEPT_BACKOFF_MS), .awake) catch break;
            continue;
        }

        // Non-blocking I/O — SSL_read/write return WANT_READ/WANT_WRITE
        // instead of blocking, allowing the virtual thread to yield.
        setNonBlocking(client_fd) catch |err| {
            log.err("fcntl(O_NONBLOCK) failed: {}", .{err});
            _ = c.close(client_fd);
            continue;
        };

        // TCP_NODELAY — disable Nagle; send TLS records immediately.
        // Critical for low-latency request/response protocols.
        if (c.setsockopt(client_fd, c.IPPROTO_TCP, c.TCP_NODELAY, &one, @sizeOf(c_int)) < 0) {
            log.err("setsockopt(TCP_NODELAY) failed for fd {}", .{client_fd});
        }

        group.async(io, handleConnection, .{ io, ctx, client_fd });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-connection virtual-thread handler
// ─────────────────────────────────────────────────────────────────────────────

fn handleConnection(
    io:  Io,
    ctx: ?*c.SSL_CTX,
    fd:  c_int,
) error{Canceled}!void {
    defer _ = c.close(fd);

    const ssl = c.SSL_new(ctx) orelse {
        log.debug("SSL_new failed for fd {}", .{fd});
        return;
    };
    defer c.SSL_free(ssl);

    _ = c.SSL_set_fd(ssl, fd);

    if (try tlsAccept(io, ssl, fd)) |err_tag| {
        // Only log genuine errors — not normal client disconnects mid-handshake.
        if (!std.mem.eql(u8, err_tag, "client eof") and
            !std.mem.eql(u8, err_tag, "client closed"))
        {
            log.debug("TLS handshake failed for fd {} ({s})", .{ fd, err_tag });
        }
        return;
    }

    log.debug("TLS connection established (fd {})", .{fd});

    var buf: [READ_BUF_SIZE]u8 = undefined;
    while (true) {
        const n = try sslRead(io, ssl, fd, &buf);
        if (n == 0) break;
        try sslWriteAll(io, ssl, fd, buf[0..@intCast(n)]);
    }

    // Bidirectional shutdown — send close_notify and wait for the peer's.
    // Prevents truncation attacks on the plaintext stream.
    _ = try tlsShutdown(io, ssl, fd);

    log.debug("TLS connection closed (fd {})", .{fd});
}

// ─────────────────────────────────────────────────────────────────────────────
// Cooperative TLS helpers — epoll-driven
//
// Instead of sleeping a fixed interval and retrying blindly, each helper
// calls waitReady(fd) which blocks in epoll_wait until the kernel signals
// that the fd is readable or writable.  On a 4-core machine this means the
// 4 OS threads are doing actual TLS work, not spinning on a timer.
// ─────────────────────────────────────────────────────────────────────────────

/// Monotonic clock in nanoseconds — immune to wall-clock adjustments.
fn nowNs() u64 {
    var ts: c.struct_timespec = undefined;
    _ = c.clock_gettime(c.CLOCK_MONOTONIC, &ts);
    return @as(u64, @intCast(ts.tv_sec)) * 1_000_000_000 +
           @as(u64, @intCast(ts.tv_nsec));
}

/// Absolute deadline = now + offset_ms milliseconds.
fn deadlineNs(offset_ms: i64) u64 {
    return nowNs() + @as(u64, @intCast(offset_ms)) * 1_000_000;
}

/// Block the virtual thread until fd is readable or writable, then return.
/// Uses a per-call epoll instance (cheap — just one fd + one event).
/// Yields the OS thread to other virtual threads while waiting.
fn waitReady(io: Io, fd: c_int, events: u32) error{Canceled}!void {
    const epfd = c.epoll_create1(0);
    if (epfd < 0) {
        // epoll unavailable — fall back to a cooperative yield.
        io.sleep(.fromMilliseconds(IO_POLL_INTERVAL_MS), .awake) catch
            return error.Canceled;
        return;
    }
    defer _ = c.close(epfd);

    var ev = c.epoll_event{
        .events = events | c.EPOLLONESHOT,
        .data = .{ .fd = fd },
    };
    _ = c.epoll_ctl(epfd, c.EPOLL_CTL_ADD, fd, &ev);

    var out: [1]c.epoll_event = undefined;
    while (true) {
        const n = c.epoll_wait(epfd, &out, 1, EPOLL_WAIT_MS);
        if (n > 0) return; // fd is ready
        if (n == 0) {
            // Timeout — yield cooperatively so other virtual threads run,
            // then re-arm and wait again.
            io.sleep(.fromMilliseconds(0), .awake) catch return error.Canceled;
            _ = c.epoll_ctl(epfd, c.EPOLL_CTL_MOD, fd, &ev);
            continue;
        }
        // EINTR — signal interrupted epoll_wait, retry.
        const e = std.posix.errno(@as(i32, -1));
        if (e == .INTR) continue;
        return; // unexpected error — proceed anyway
    }
}

/// Perform the TLS server handshake, yielding via epoll until the fd is ready.
/// Returns null on success, or a static error string on permanent failure.
/// Enforces HANDSHAKE_TIMEOUT_MS — drops clients that stall the handshake.
fn tlsAccept(io: Io, ssl: *c.SSL, fd: c_int) error{Canceled}!?[]const u8 {
    const deadline = deadlineNs(HANDSHAKE_TIMEOUT_MS);
    while (true) {
        if (nowNs() > deadline) return "handshake timeout";

        const rc = c.SSL_accept(ssl);
        if (rc == 1) return null;

        switch (c.SSL_get_error(ssl, rc)) {
            c.SSL_ERROR_WANT_READ  => try waitReady(io, fd, c.EPOLLIN),
            c.SSL_ERROR_WANT_WRITE => try waitReady(io, fd, c.EPOLLOUT),
            c.SSL_ERROR_ZERO_RETURN => {
                // Client sent close_notify mid-handshake — clean disconnect.
                _ = c.ERR_clear_error();
                return "client closed";
            },
            c.SSL_ERROR_SYSCALL => {
                // EOF with no close_notify — client disconnected abruptly.
                // This is normal for short-lived test clients; don't log it.
                _ = c.ERR_clear_error();
                return "client eof";
            },
            else => {
                // Genuine TLS error — log it.
                c.ERR_print_errors_fp(c.stderr);
                return "SSL_accept";
            },
        }
    }
}

/// Read up to buf.len bytes, waking only when the fd is readable.
/// Returns 0 on clean EOF or fatal error.
fn sslRead(io: Io, ssl: *c.SSL, fd: c_int, buf: []u8) error{Canceled}!c_int {
    while (true) {
        const n = c.SSL_read(ssl, buf.ptr, @intCast(buf.len));
        if (n > 0) return n;

        switch (c.SSL_get_error(ssl, n)) {
            c.SSL_ERROR_WANT_READ  => try waitReady(io, fd, c.EPOLLIN),
            c.SSL_ERROR_WANT_WRITE => try waitReady(io, fd, c.EPOLLOUT),
            c.SSL_ERROR_ZERO_RETURN => return 0,
            c.SSL_ERROR_SYSCALL => {
                _ = c.ERR_clear_error();
                return 0; // abrupt client disconnect — treat as EOF
            },
            else => {
                c.ERR_print_errors_fp(c.stderr);
                return 0;
            },
        }
    }
}

/// Write all bytes in buf, looping on partial writes.
fn sslWriteAll(io: Io, ssl: *c.SSL, fd: c_int, buf: []const u8) error{Canceled}!void {
    var remaining = buf;
    while (remaining.len > 0) {
        const n = c.SSL_write(ssl, remaining.ptr, @intCast(remaining.len));
        if (n > 0) {
            remaining = remaining[@intCast(n)..];
            continue;
        }
        switch (c.SSL_get_error(ssl, n)) {
            c.SSL_ERROR_WANT_READ  => try waitReady(io, fd, c.EPOLLIN),
            c.SSL_ERROR_WANT_WRITE => try waitReady(io, fd, c.EPOLLOUT),
            else => {
                c.ERR_print_errors_fp(c.stderr);
                return;
            },
        }
    }
}

/// Bidirectional TLS shutdown, yielding via epoll.
fn tlsShutdown(io: Io, ssl: *c.SSL, fd: c_int) error{Canceled}!void {
    var attempts: usize = 0;
    while (attempts < 2) : (attempts += 1) {
        const rc = c.SSL_shutdown(ssl);
        if (rc == 1) return;
        if (rc == 0) continue;

        switch (c.SSL_get_error(ssl, rc)) {
            c.SSL_ERROR_WANT_READ  => {
                try waitReady(io, fd, c.EPOLLIN);
                attempts -|= 1;
            },
            c.SSL_ERROR_WANT_WRITE => {
                try waitReady(io, fd, c.EPOLLOUT);
                attempts -|= 1;
            },
            else => return,
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

fn setNonBlocking(fd: c_int) !void {
    const flags = c.fcntl(fd, c.F_GETFL, @as(c_int, 0));
    if (flags < 0) return error.FcntlGetFailed;
    if (c.fcntl(fd, c.F_SETFL, flags | c.O_NONBLOCK) < 0)
        return error.FcntlSetFailed;
}

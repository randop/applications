//! tls_server.zig — io_uring + TLS echo server, millions-of-connections design
//!
//! Architecture:
//!
//!   ┌──────────────────────────────────────────────────────────────────┐
//!   │  main()  —  Io.Threaded (N OS threads, virtual-thread runtime)  │
//!   │                                                                  │
//!   │   reactor vthread (1)                                            │
//!   │   ├─ owns the io_uring ring                                      │
//!   │   ├─ multishot ACCEPT SQE  →  one SQE yields ∞ client CQEs      │
//!   │   ├─ RECV / SEND SQEs per connection                             │
//!   │   └─ drains CQE ring, sets conn.ready, wakes parked vthreads     │
//!   │                                                                  │
//!   │   handleConnection vthreads (one per live connection)            │
//!   │   ├─ TLS handshake  (uringTlsAccept)                             │
//!   │   ├─ echo loop      (uringRead / uringWriteAll)                  │
//!   │   └─ park via parkUntilReady — reactor wakes them on CQE         │
//!   └──────────────────────────────────────────────────────────────────┘
//!
//! Why io_uring beats epoll at scale:
//!   • epoll_create1 + epoll_ctl + epoll_wait = 3 syscalls per fd event.
//!     io_uring uses shared-memory rings → 0 syscalls in the steady state
//!     (with SQPOLL or IORING_FEAT_FAST_POLL, kernel ≥ 5.7).
//!   • Multishot accept: one SQE reaps every new connection forever.
//!     epoll requires re-registering the listener after each accept.
//!   • IORING_SETUP_COOP_TASKRUN batches CQE delivery, reducing
//!     interrupt overhead under high connection counts.
//!   • IORING_FEAT_FAST_POLL: the kernel polls sockets inline before
//!     parking — no epoll layer at all for low-latency connections.
//!
//! Kernel requirements:
//!   • IORING_OP_ACCEPT multishot:  Linux ≥ 5.19
//!   • IORING_FEAT_FAST_POLL:       Linux ≥ 5.7
//!   • IORING_SETUP_COOP_TASKRUN:   Linux ≥ 5.19
//!
//! System limits (before running at scale):
//!   sudo sysctl -w fs.file-max=2000000
//!   sudo sysctl -w net.core.somaxconn=65535
//!   sudo sysctl -w net.ipv4.tcp_max_syn_backlog=65535
//!   ulimit -n 1000000
//!
//! Build:
//!   zig build -Doptimize=ReleaseFast
//!
//! Requires: Zig 0.16.0-dev, OpenSSL ≥ 3.x, server.crt / server.key in CWD.

const std = @import("std");
const Io  = std.Io;

const c = @cImport({
    @cInclude("stdio.h");
    @cInclude("time.h");
    @cInclude("string.h");
    @cInclude("openssl/ssl.h");
    @cInclude("openssl/err.h");
    @cInclude("sys/socket.h");
    @cInclude("netinet/in.h");
    @cInclude("arpa/inet.h");
    @cInclude("netinet/tcp.h");
    @cInclude("unistd.h");
    @cInclude("fcntl.h");
    @cInclude("errno.h");
    @cInclude("sys/mman.h");
    @cInclude("sys/syscall.h");
    @cInclude("linux/io_uring.h");
    @cInclude("poll.h");
});

const build_options = @import("build_options");
const logger = @import("log.zig");
const log    = logger.scoped(.main);

// ─────────────────────────────────────────────────────────────────────────────
// Tuning knobs
// ─────────────────────────────────────────────────────────────────────────────

/// io_uring SQ/CQ depth — must be a power of two.
/// 4096 allows 4096 in-flight async operations before the producer stalls.
const RING_DEPTH: u32 = 4_096;

/// Initial connection table capacity (grows dynamically).
/// Pre-allocates enough for 64 K simultaneous connections with no realloc.
const CONN_TABLE_INITIAL: usize = 65_536;

/// Maximum ms to wait for a TLS handshake before evicting the client.
const HANDSHAKE_TIMEOUT_MS: u64 = 5_000;

/// listen() backlog — SOMAXCONN = kernel maximum.
const LISTEN_BACKLOG: c_int = c.SOMAXCONN;

/// Back-off delay (ms) when accept returns a transient error.
const ACCEPT_BACKOFF_MS: i64 = 5;

/// Per-connection read buffer (stack-allocated in the vthread, never heap).
/// 16 KiB matches the maximum TLS record size.
const READ_BUF_SIZE: usize = 16 * 1_024;

// ─────────────────────────────────────────────────────────────────────────────
// io_uring ring
//
// Zig 0.16.0-dev exposes the raw Linux syscall interface via std.os.linux.
// We map the SQ and CQ rings into user-space via mmap, then read/write the
// head/tail pointers with atomic loads/stores to avoid entering the kernel.
// ─────────────────────────────────────────────────────────────────────────────

/// Tag embedded in every SQE user_data so the CQE dispatch switch knows
/// what kind of operation completed.
const OpTag = enum(u8) {
    accept = 1,
    recv   = 2,
    send   = 3,
};

/// 64-bit value stored in sqe.user_data and returned verbatim in cqe.user_data.
const UserData = packed struct(u64) {
    tag:  OpTag, //  8 bits
    fd:   i32,   // 32 bits
    _pad: u24 = 0,
};

const Ring = struct {
    fd:      c_int,

    sq_len:  u32,
    sq_mask: u32,
    sqes:    [*]c.io_uring_sqe,
    sq_head: *u32,
    sq_tail: *u32,
    sq_arr:  [*]u32,

    cq_len:  u32,
    cq_mask: u32,
    cq_head: *u32,
    cq_tail: *u32,
    cqes:    [*]c.io_uring_cqe,

    /// Set up the ring and mmap the SQ, SQE array, and CQ regions.
    fn init(depth: u32) !Ring {
        var params: c.io_uring_params = std.mem.zeroes(c.io_uring_params);
        // COOP_TASKRUN + TASKRUN_FLAG: deliver CQEs in task context (≥ 5.19).
        // Under heavy load this batches completions and reduces interrupt noise.
        params.flags = c.IORING_SETUP_COOP_TASKRUN | c.IORING_SETUP_TASKRUN_FLAG;

        const setup_ret = std.os.linux.syscall2(
            .io_uring_setup,
            @intCast(depth),
            @intFromPtr(&params),
        );
        const ring_fd: c_int = @intCast(@as(isize, @bitCast(setup_ret)));
        if (ring_fd < 0) {
            log.err("io_uring_setup syscall failed ({}); is kernel ≥ 5.19?", .{ring_fd});
            return error.IoUringSetupFailed;
        }

        // Map SQ ring (head/tail/mask/array) ─────────────────────────────────
        const sq_ring_sz = params.sq_off.array + params.sq_entries * @sizeOf(u32);
        const sq_map = mmap(ring_fd, c.IORING_OFF_SQ_RING, sq_ring_sz) orelse
            return error.MmapSqFailed;

        const sq_base = @intFromPtr(sq_map);
        const sq_head: *u32 = @ptrFromInt(sq_base + params.sq_off.head);
        const sq_tail: *u32 = @ptrFromInt(sq_base + params.sq_off.tail);
        const sq_mask: u32  = @as(*u32, @ptrFromInt(sq_base + params.sq_off.ring_mask)).*;
        const sq_arr:  [*]u32 = @ptrFromInt(sq_base + params.sq_off.array);

        // Map SQE array ───────────────────────────────────────────────────────
        const sqe_map = mmap(ring_fd, c.IORING_OFF_SQES,
                             params.sq_entries * @sizeOf(c.io_uring_sqe)) orelse
            return error.MmapSqesFailed;
        const sqes: [*]c.io_uring_sqe = @alignCast(@ptrCast(sqe_map));

        // Map CQ ring ─────────────────────────────────────────────────────────
        const cq_ring_sz = params.cq_off.cqes + params.cq_entries * @sizeOf(c.io_uring_cqe);
        const cq_map = mmap(ring_fd, c.IORING_OFF_CQ_RING, cq_ring_sz) orelse
            return error.MmapCqFailed;

        const cq_base = @intFromPtr(cq_map);
        const cq_head: *u32 = @ptrFromInt(cq_base + params.cq_off.head);
        const cq_tail: *u32 = @ptrFromInt(cq_base + params.cq_off.tail);
        const cq_mask: u32  = @as(*u32, @ptrFromInt(cq_base + params.cq_off.ring_mask)).*;
        const cqes: [*]c.io_uring_cqe  = @ptrFromInt(cq_base + params.cq_off.cqes);

        const fast_poll = params.features & c.IORING_FEAT_FAST_POLL != 0;
        log.info("io_uring ready  fd={}  sq={}  cq={}  fast_poll={}",
            .{ ring_fd, params.sq_entries, params.cq_entries, fast_poll });

        return Ring{
            .fd      = ring_fd,
            .sq_len  = params.sq_entries,
            .sq_mask = sq_mask,
            .sqes    = sqes,
            .sq_head = sq_head,
            .sq_tail = sq_tail,
            .sq_arr  = sq_arr,
            .cq_len  = params.cq_entries,
            .cq_mask = cq_mask,
            .cq_head = cq_head,
            .cq_tail = cq_tail,
            .cqes    = cqes,
        };
    }

    /// Claim the next SQE slot.  Returns null if the submission queue is full.
    fn getSqe(self: *Ring) ?*c.io_uring_sqe {
        const tail = @atomicLoad(u32, self.sq_tail, .acquire);
        const head = @atomicLoad(u32, self.sq_head, .acquire);
        if (tail -% head >= self.sq_len) return null;
        const idx          = tail & self.sq_mask;
        self.sq_arr[idx]   = idx;
        const sqe          = &self.sqes[idx];
        @memset(std.mem.asBytes(sqe), 0);
        return sqe;
    }

    /// Make the last claimed SQE visible to the kernel.
    fn flush(self: *Ring) void {
        const tail = @atomicLoad(u32, self.sq_tail, .acquire);
        @atomicStore(u32, self.sq_tail, tail +% 1, .release);
    }

    /// Submit pending SQEs and optionally wait for completions.
    fn enter(self: *Ring, to_submit: u32, min_complete: u32) !void {
        const flags: u32 = if (min_complete > 0) c.IORING_ENTER_GETEVENTS else 0;
        const ret = std.os.linux.syscall6(
            .io_uring_enter,
            @intCast(self.fd),
            @intCast(to_submit),
            @intCast(min_complete),
            @intCast(flags),
            0, 0,
        );
        if (@as(isize, @bitCast(ret)) < 0) return error.IoUringEnterFailed;
    }

    /// Drain every available CQE, calling handler(cqe) for each one.
    fn drain(self: *Ring, handler: anytype) void {
        while (true) {
            const head = @atomicLoad(u32, self.cq_head, .acquire);
            const tail = @atomicLoad(u32, self.cq_tail, .acquire);
            if (head == tail) break;
            handler(&self.cqes[head & self.cq_mask]);
            @atomicStore(u32, self.cq_head, head +% 1, .release);
        }
    }
};

fn mmap(ring_fd: c_int, offset: u64, size: usize) ?*anyopaque {
    const p = c.mmap(null, size,
        c.PROT_READ | c.PROT_WRITE,
        c.MAP_SHARED | c.MAP_POPULATE,
        ring_fd, @intCast(offset));
    return if (p == c.MAP_FAILED) null else p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Connection table
//
// Direct-mapped by fd number — O(1) lookup, no hashing.
// Grows via realloc when a new fd exceeds the current capacity.
// ─────────────────────────────────────────────────────────────────────────────

const Conn = struct {
    ssl:      ?*c.SSL = null,
    /// Set by the reactor when a POLL_ADD CQE arrives for this fd.
    /// Cleared by parkUntilReady before each new park.
    ready:    std.atomic.Value(bool) = .{ .raw = false },
    /// Set by the vthread just before it begins waiting.
    /// The reactor only calls ready.store(true) if parked is true,
    /// preventing spurious pre-wakeups before SSL_accept even runs.
    parked:   std.atomic.Value(bool) = .{ .raw = false },
    deadline: u64 = 0,
};

const ConnTable = struct {
    slots: []Conn,
    alloc: std.mem.Allocator,

    fn init(alloc: std.mem.Allocator, cap: usize) !ConnTable {
        const slots = try alloc.alloc(Conn, cap);
        @memset(slots, Conn{});
        return .{ .slots = slots, .alloc = alloc };
    }

    fn deinit(self: *ConnTable) void {
        self.alloc.free(self.slots);
    }

    fn ensure(self: *ConnTable, fd: usize) !void {
        if (fd < self.slots.len) return;
        const new_len  = @max(fd + 1, self.slots.len * 2);
        const old_len  = self.slots.len;
        self.slots     = try self.alloc.realloc(self.slots, new_len);
        @memset(self.slots[old_len..], Conn{});
    }

    fn get(self: *ConnTable, fd: c_int) *Conn {
        return &self.slots[@intCast(fd)];
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SQE field writer
//
// io_uring_sqe uses anonymous unions in C. Zig's cImport gives anonymous
// unions sequential names (unnamed_0, unnamed_1, …) that vary across kernel
// header versions and are not stable to reference by name.
//
// We write the union fields at their known ABI byte offsets instead.
// Offsets are from the Linux UAPI definition (stable since 5.1):
//
//   0   opcode     u8
//   1   flags      u8
//   2   ioprio     u16
//   4   fd         i32
//   8   off/addr2  u64   ← first union
//  16   addr       u64   ← second union
//  24   len        u32
//  28   rw_flags/accept_flags/… u32  ← third union
//  32   user_data  u64
//  40   buf_index  u16   (packed)
//  42   personality u16
//  44   splice_fd_in / file_index u32
//  48   addr3      u64
//  56   __pad2     u64
// ─────────────────────────────────────────────────────────────────────────────

const SQE_OFF_OPCODE    = 0;
const SQE_OFF_IOPRIO    = 2;
const SQE_OFF_FD        = 4;
const SQE_OFF_ADDR2     = 8;   // first union  (off / addr2)
const SQE_OFF_ADDR      = 16;  // second union (addr / splice_off_in)
const SQE_OFF_LEN       = 24;
const SQE_OFF_OP_FLAGS  = 28;  // third union  (accept_flags / rw_flags / …)
const SQE_OFF_USER_DATA = 32;

inline fn sqeSet(sqe: *c.io_uring_sqe, comptime T: type, offset: usize, val: T) void {
    const base: [*]u8 = @ptrCast(sqe);
    const ptr: *align(1) T = @ptrCast(base + offset);
    ptr.* = val;
}


/// Set once in main(), read-only afterwards.
/// Using a global avoids passing the reactor through every call frame.
var g_ring:  Ring       = undefined;
var g_conns: ConnTable  = undefined;
var g_ctx:   ?*c.SSL_CTX = null;

// A small FIFO of newly accepted fds waiting to be handed off to vthreads.
// The reactor pushes into it; the accept-dispatch loop pops from it.
const FD_QUEUE_CAP = 8_192;
var g_new_fd_buf: [FD_QUEUE_CAP]c_int = undefined;
var g_new_fd_head: usize = 0;
var g_new_fd_tail: usize = 0;

fn newFdPush(fd: c_int) void {
    const next = (g_new_fd_tail + 1) % FD_QUEUE_CAP;
    if (next == g_new_fd_head) {
        // Queue full — drop the connection.  Raise FD_QUEUE_CAP to avoid this.
        log.err("new-fd queue full, dropping fd {}", .{fd});
        _ = c.close(fd);
        return;
    }
    g_new_fd_buf[g_new_fd_tail] = fd;
    g_new_fd_tail = next;
}

fn newFdPop() ?c_int {
    if (g_new_fd_head == g_new_fd_tail) return null;
    const fd = g_new_fd_buf[g_new_fd_head];
    g_new_fd_head = (g_new_fd_head + 1) % FD_QUEUE_CAP;
    return fd;
}

// ─────────────────────────────────────────────────────────────────────────────
// CQE dispatch handler (called from ring.drain inside the accept loop)
// ─────────────────────────────────────────────────────────────────────────────

fn onCqe(cqe: *const c.io_uring_cqe) void {
    const ud: UserData = @bitCast(cqe.user_data);
    switch (ud.tag) {

        .accept => {
            // Multishot accept: cqe.res is the new client fd (≥ 0) or an error.
            // IORING_CQE_F_MORE (bit 1 of cqe.flags) is set if the multishot
            // SQE is still armed — no need to re-submit in that case.
            if (cqe.res < 0) {
                if (cqe.res != -@as(i32, @intCast(c.EAGAIN)) and
                    cqe.res != -@as(i32, @intCast(c.EWOULDBLOCK)))
                {
                    log.err("accept CQE error: {d}", .{cqe.res});
                }
                return;
            }
            const fd: c_int = cqe.res;
            const one: c_int = 1;
            _ = c.setsockopt(fd, c.IPPROTO_TCP, c.TCP_NODELAY,
                &one, @sizeOf(c_int));
            g_conns.ensure(@intCast(fd)) catch {
                _ = c.close(fd);
                return;
            };
            g_conns.get(fd).* = .{};  // reset slot
            newFdPush(fd);
        },

        .recv, .send => {
            // Only wake the vthread if it is actually parked waiting.
            // If it hasn't reached parkUntilReady yet, the CQE is spurious —
            // drop it; the vthread will submit a fresh POLL_ADD when it parks.
            const conn = g_conns.get(ud.fd);
            if (conn.parked.load(.acquire)) {
                conn.ready.store(true, .release);
            }
        },
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

pub fn main(init: std.process.Init.Minimal) !void {
    const gpa = std.heap.c_allocator;

    var threaded: Io.Threaded = .init(gpa, .{ .environ = init.environ });
    defer threaded.deinit();
    const io = threaded.io();

    log.info("TLS server version {s}", .{build_options.version});

    // ── io_uring ──────────────────────────────────────────────────────────────
    g_ring  = try Ring.init(RING_DEPTH);
    g_conns = try ConnTable.init(gpa, CONN_TABLE_INITIAL);
    defer g_conns.deinit();

    // ── OpenSSL ───────────────────────────────────────────────────────────────
    if (c.OPENSSL_init_ssl(c.OPENSSL_INIT_SSL_DEFAULT, null) != 1)
        return error.OpenSSLInitFailed;

    g_ctx = c.SSL_CTX_new(c.TLS_server_method()) orelse
        return error.SSLContextFailed;
    defer c.SSL_CTX_free(g_ctx);

    _ = c.SSL_CTX_set_min_proto_version(g_ctx, c.TLS1_2_VERSION);
    _ = c.SSL_CTX_set_mode(g_ctx,
        c.SSL_MODE_RELEASE_BUFFERS          |
        c.SSL_MODE_ENABLE_PARTIAL_WRITE     |
        c.SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER,
    );

    if (c.SSL_CTX_use_certificate_file(g_ctx, "server.crt", c.SSL_FILETYPE_PEM) != 1) {
        c.ERR_print_errors_fp(c.stderr);
        return error.CertLoadFailed;
    }
    if (c.SSL_CTX_use_PrivateKey_file(g_ctx, "server.key", c.SSL_FILETYPE_PEM) != 1) {
        c.ERR_print_errors_fp(c.stderr);
        return error.KeyLoadFailed;
    }
    if (c.SSL_CTX_check_private_key(g_ctx) != 1) {
        c.ERR_print_errors_fp(c.stderr);
        return error.KeyMismatch;
    }

    // ── Listening socket ──────────────────────────────────────────────────────
    // SOCK_NONBLOCK so the kernel doesn't block on accept internally.
    const listener = c.socket(
        c.AF_INET,
        c.SOCK_STREAM | c.SOCK_NONBLOCK | c.SOCK_CLOEXEC,
        0,
    );
    if (listener < 0) return error.SocketFailed;
    defer _ = c.close(listener);

    const one: c_int = 1;
    if (c.setsockopt(listener, c.SOL_SOCKET, c.SO_REUSEADDR, &one, @sizeOf(c_int)) < 0)
        return error.SetsockoptFailed;
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

    // ── Arm multishot accept ──────────────────────────────────────────────────
    //
    // IORING_OP_ACCEPT with IORING_ACCEPT_MULTISHOT: one SQE → the kernel
    // posts a CQE for every new connection without us re-submitting.
    // IORING_CQE_F_MORE (bit 1 of cqe.flags) stays set as long as the SQE
    // is armed; we only need to re-arm if the SQE is cancelled.
    {
        const sqe = g_ring.getSqe() orelse return error.RingFull;
        sqeSet(sqe, u8,  SQE_OFF_OPCODE,    @intCast(c.IORING_OP_ACCEPT));
        sqeSet(sqe, i32, SQE_OFF_FD,        listener);
        sqeSet(sqe, u16, SQE_OFF_IOPRIO,    c.IORING_ACCEPT_MULTISHOT);
        sqeSet(sqe, u64, SQE_OFF_ADDR2,     0);  // null sockaddr
        sqeSet(sqe, u64, SQE_OFF_ADDR,      0);
        sqeSet(sqe, u32, SQE_OFF_OP_FLAGS,  @intCast(c.SOCK_NONBLOCK | c.SOCK_CLOEXEC));
        sqeSet(sqe, u64, SQE_OFF_USER_DATA, @bitCast(UserData{ .tag = .accept, .fd = listener }));
        g_ring.flush();
        try g_ring.enter(1, 0); // submit the SQE
    }

    log.info("TLS echo server listening on 0.0.0.0:8443 (io_uring multishot)", .{});

    // ── Reactor vthread + connection dispatch ─────────────────────────────────
    //
    // The reactor runs as a dedicated virtual thread.  It calls io.sleep(0)
    // after each drain to yield the OS thread, allowing handleConnection
    // vthreads to make progress.  This is the key fix: a blocking ring.enter
    // in the main loop would starve all other vthreads on the same OS thread.
    var group: Io.Group = .init;
    defer group.cancel(io);

    // Reactor vthread: drives the ring and populates the new-fd queue.
    var reactor_future = io.async(reactorLoop, .{io});
    defer reactor_future.cancel(io) catch {};

    // Dispatcher vthread: spawns handleConnection for each new fd.
    while (true) {
        while (newFdPop()) |fd| {
            group.async(io, handleConnection, .{ io, fd });
        }
        // Yield so the reactor and connection vthreads can run.
        io.sleep(.fromMilliseconds(0), .awake) catch break;
    }
}

/// Reactor loop — runs as a virtual thread alongside handleConnection vthreads.
/// Drains the CQ ring and sets conn.ready to wake parked vthreads.
/// Yields after each drain via io.sleep(0) so other vthreads are not starved.
fn reactorLoop(io: Io) error{Canceled}!void {
    while (true) {
        // Non-blocking: submit 0, wait 0. Returns immediately if CQ is empty.
        // We do NOT block here — that would pin an OS thread and starve vthreads.
        g_ring.enter(0, 0) catch {};

        g_ring.drain(onCqe);

        // Cooperative yield — give other virtual threads a turn.
        io.sleep(.fromMilliseconds(0), .awake) catch return error.Canceled;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-connection virtual-thread handler
// ─────────────────────────────────────────────────────────────────────────────

fn handleConnection(io: Io, fd: c_int) error{Canceled}!void {
    defer {
        const conn = g_conns.get(fd);
        if (conn.ssl) |ssl| {
            _ = c.SSL_shutdown(ssl);
            c.SSL_free(ssl);
            conn.ssl = null;
        }
        _ = c.close(fd);
        log.debug("connection closed (fd {})", .{fd});
    }

    const conn = g_conns.get(fd);

    const ssl = c.SSL_new(g_ctx) orelse {
        log.debug("SSL_new failed (fd {})", .{fd});
        return;
    };
    conn.ssl      = ssl;
    conn.deadline = nowNs() + HANDSHAKE_TIMEOUT_MS * 1_000_000;
    _ = c.SSL_set_fd(ssl, fd);

    // ── TLS handshake ─────────────────────────────────────────────────────────
    if (try uringTlsAccept(io, ssl, fd, conn)) |err_tag| {
        if (!std.mem.eql(u8, err_tag, "client eof") and
            !std.mem.eql(u8, err_tag, "client closed"))
        {
            log.debug("handshake failed (fd {} — {s})", .{ fd, err_tag });
        }
        return;
    }

    log.debug("TLS connection established (fd {})", .{fd});

    // ── Echo loop ─────────────────────────────────────────────────────────────
    var buf: [READ_BUF_SIZE]u8 = undefined;
    while (true) {
        const n = try uringRead(io, ssl, fd, conn, &buf);
        if (n == 0) break;
        try uringWriteAll(io, ssl, fd, conn, buf[0..@intCast(n)]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// io_uring-backed TLS helpers
//
// CRITICAL DESIGN CONSTRAINT: OpenSSL owns the socket's read buffer.
// We must NEVER submit IORING_OP_RECV/SEND to detect readiness — the kernel
// would consume bytes from the socket before OpenSSL sees them, corrupting
// the TLS record layer.
//
// Instead we use IORING_OP_POLL_ADD, which only watches for fd readiness
// (POLLIN / POLLOUT) and never touches the data stream.  When the CQE
// arrives (conn.ready = true), we call SSL_read/SSL_write which reads/writes
// the data through OpenSSL's own buffering.
//
// For actual data transfer in uringRead/uringWriteAll, the same rule applies:
// submit POLL_ADD to wait for readiness, then call SSL_read/SSL_write.
//
// POLL_ADD offset in the SQE (addr field holds the poll mask as u32):
//   poll_events lives in the third union at SQE_OFF_OP_FLAGS (offset 28),
//   but for POLL_ADD the mask goes in the addr field (offset 16) as u32.
// ─────────────────────────────────────────────────────────────────────────────

/// Yield the virtual thread until the reactor marks this connection ready.
/// Sets conn.parked = true before yielding so the reactor knows it is safe
/// to deliver a wakeup; clears it after waking to prevent stale wakeups.
inline fn parkUntilReady(io: Io, conn: *Conn) error{Canceled}!void {
    conn.parked.store(true, .release);
    while (!conn.ready.load(.acquire)) {
        io.sleep(.fromMilliseconds(0), .awake) catch {
            conn.parked.store(false, .release);
            return error.Canceled;
        };
    }
    conn.parked.store(false, .release);
    conn.ready.store(false, .release);
}

/// Submit IORING_OP_POLL_ADD watching for POLLIN readiness.
/// Does NOT consume any bytes — safe to use while OpenSSL owns the socket.
inline fn submitPollIn(fd: c_int) void {
    if (g_ring.getSqe()) |sqe| {
        sqeSet(sqe, u8,  SQE_OFF_OPCODE,    @intCast(c.IORING_OP_POLL_ADD));
        sqeSet(sqe, i32, SQE_OFF_FD,        fd);
        // poll_events: POLLIN — fd has data to read.
        // For POLL_ADD the mask is stored in the addr field (lower 32 bits).
        sqeSet(sqe, u32, SQE_OFF_ADDR,      c.POLLIN);
        sqeSet(sqe, u64, SQE_OFF_USER_DATA, @bitCast(UserData{ .tag = .recv, .fd = fd }));
        g_ring.flush();
        g_ring.enter(1, 0) catch {};
    }
}

/// Submit IORING_OP_POLL_ADD watching for POLLOUT readiness.
inline fn submitPollOut(fd: c_int) void {
    if (g_ring.getSqe()) |sqe| {
        sqeSet(sqe, u8,  SQE_OFF_OPCODE,    @intCast(c.IORING_OP_POLL_ADD));
        sqeSet(sqe, i32, SQE_OFF_FD,        fd);
        sqeSet(sqe, u32, SQE_OFF_ADDR,      c.POLLOUT);
        sqeSet(sqe, u64, SQE_OFF_USER_DATA, @bitCast(UserData{ .tag = .send, .fd = fd }));
        g_ring.flush();
        g_ring.enter(1, 0) catch {};
    }
}

fn uringTlsAccept(io: Io, ssl: *c.SSL, fd: c_int, conn: *Conn) error{Canceled}!?[]const u8 {
    while (true) {
        if (nowNs() > conn.deadline) return "handshake timeout";

        const rc = c.SSL_accept(ssl);
        if (rc == 1) return null;

        switch (c.SSL_get_error(ssl, rc)) {
            c.SSL_ERROR_WANT_READ  => { submitPollIn(fd);  try parkUntilReady(io, conn); },
            c.SSL_ERROR_WANT_WRITE => { submitPollOut(fd); try parkUntilReady(io, conn); },
            c.SSL_ERROR_ZERO_RETURN => { _ = c.ERR_clear_error(); return "client closed"; },
            c.SSL_ERROR_SYSCALL     => { _ = c.ERR_clear_error(); return "client eof"; },
            else => { c.ERR_print_errors_fp(c.stderr); return "SSL_accept"; },
        }
    }
}

fn uringRead(io: Io, ssl: *c.SSL, fd: c_int, conn: *Conn, buf: []u8) error{Canceled}!c_int {
    while (true) {
        const n = c.SSL_read(ssl, buf.ptr, @intCast(buf.len));
        if (n > 0) return n;

        switch (c.SSL_get_error(ssl, n)) {
            c.SSL_ERROR_WANT_READ  => { submitPollIn(fd);  try parkUntilReady(io, conn); },
            c.SSL_ERROR_WANT_WRITE => { submitPollOut(fd); try parkUntilReady(io, conn); },
            c.SSL_ERROR_ZERO_RETURN => return 0,
            c.SSL_ERROR_SYSCALL     => { _ = c.ERR_clear_error(); return 0; },
            else => { c.ERR_print_errors_fp(c.stderr); return 0; },
        }
    }
}

fn uringWriteAll(io: Io, ssl: *c.SSL, fd: c_int, conn: *Conn, buf: []const u8) error{Canceled}!void {
    var rem = buf;
    while (rem.len > 0) {
        const n = c.SSL_write(ssl, rem.ptr, @intCast(rem.len));
        if (n > 0) { rem = rem[@intCast(n)..]; continue; }

        switch (c.SSL_get_error(ssl, n)) {
            c.SSL_ERROR_WANT_READ  => { submitPollIn(fd);  try parkUntilReady(io, conn); },
            c.SSL_ERROR_WANT_WRITE => { submitPollOut(fd); try parkUntilReady(io, conn); },
            else => { c.ERR_print_errors_fp(c.stderr); return; },
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

fn nowNs() u64 {
    var ts: c.struct_timespec = undefined;
    _ = c.clock_gettime(c.CLOCK_MONOTONIC, &ts);
    return @as(u64, @intCast(ts.tv_sec)) * 1_000_000_000 +
           @as(u64, @intCast(ts.tv_nsec));
}

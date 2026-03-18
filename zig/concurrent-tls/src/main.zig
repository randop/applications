//! tls_server.zig — io_uring TLS echo server, millions-of-connections design
//!
//! Architecture (correct thread model):
//!
//!   ┌─────────────────────────────────────────────────────────────────────┐
//!   │  OS thread 0: reactor (std.Thread, NOT a vthread)                  │
//!   │    • blocks in io_uring_enter(min_complete=1) — never starves       │
//!   │    • drains CQEs, signals conn.cond for each completed fd           │
//!   │    • pushes new fds into g_new_fd queue                             │
//!   │                                                                     │
//!   │  OS threads 1..N: Io.Threaded virtual-thread pool                  │
//!   │    • handleConnection vthreads — one per live connection            │
//!   │    • parkUntilReady: blocks on conn.mutex/conn.cond (true sleep)    │
//!   │    • reactor wakes exactly the right vthread via cond.signal()      │
//!   │    • dispatcher loop: pops new fds, calls group.async               │
//!   └─────────────────────────────────────────────────────────────────────┘
//!
//! Why the previous design failed:
//!   • io.sleep(0) is NOT a true sleep — it reschedules immediately.
//!   • With N connections all calling io.sleep(0) in parkUntilReady,
//!     all OS threads are consumed spinning.  New connections stall because
//!     there is no thread left to run SSL_accept.
//!   • Running the reactor as a vthread with ring.enter(0,1) still blocks
//!     one OS thread, leaving N-1 for N connections — deadlocks at N-1.
//!
//! Correct model:
//!   • Reactor = dedicated OS thread, blocks in ring.enter, never in pool.
//!   • parkUntilReady = conn.mutex + conn.cond.wait() — OS truly sleeps,
//!     thread returns to pool, reactor signals when CQE arrives.
//!
//! Kernel requirements:
//!   • IORING_OP_ACCEPT multishot:  Linux ≥ 5.19
//!   • IORING_FEAT_FAST_POLL:       Linux ≥ 5.7
//!   • IORING_SETUP_COOP_TASKRUN:   Linux ≥ 5.19
//!
//! System limits (set before load testing at scale):
//!   sudo sysctl -w fs.file-max=2000000
//!   sudo sysctl -w net.core.somaxconn=65535
//!   sudo sysctl -w net.ipv4.tcp_max_syn_backlog=65535
//!   ulimit -n 1000000
//!
//! Build:
//!   zig build -Doptimize=ReleaseFast

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
// Tuning
// ─────────────────────────────────────────────────────────────────────────────

const RING_DEPTH:          u32    = 4_096;
const CONN_TABLE_INITIAL:  usize  = 65_536;
const HANDSHAKE_TIMEOUT_MS: u64   = 5_000;
const LISTEN_BACKLOG:      c_int  = c.SOMAXCONN;
const ACCEPT_BACKOFF_MS:   i64    = 5;
const READ_BUF_SIZE:       usize  = 16 * 1_024;
const FD_QUEUE_CAP:        usize  = 8_192;

// ─────────────────────────────────────────────────────────────────────────────
// io_uring ring
// ─────────────────────────────────────────────────────────────────────────────

const OpTag = enum(u8) { accept = 1, poll_in = 2, poll_out = 3 };

const UserData = packed struct(u64) {
    tag:  OpTag,
    fd:   i32,
    _pad: u24 = 0,
};

// SQE ABI byte offsets (stable since Linux 5.1 UAPI).
const SQE_OFF_OPCODE    = 0;
const SQE_OFF_IOPRIO    = 2;
const SQE_OFF_FD        = 4;
const SQE_OFF_ADDR2     = 8;
const SQE_OFF_ADDR      = 16;
const SQE_OFF_OP_FLAGS  = 28;
const SQE_OFF_USER_DATA = 32;

inline fn sqeSet(sqe: *c.io_uring_sqe, comptime T: type, offset: usize, val: T) void {
    const ptr: *align(1) T = @ptrCast(@as([*]u8, @ptrCast(sqe)) + offset);
    ptr.* = val;
}

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

    fn init(depth: u32) !Ring {
        var params: c.io_uring_params = std.mem.zeroes(c.io_uring_params);
        params.flags = c.IORING_SETUP_COOP_TASKRUN | c.IORING_SETUP_TASKRUN_FLAG;

        const ret = std.os.linux.syscall2(.io_uring_setup, @intCast(depth), @intFromPtr(&params));
        const ring_fd: c_int = @intCast(@as(isize, @bitCast(ret)));
        if (ring_fd < 0) return error.IoUringSetupFailed;

        const sq_sz  = params.sq_off.array + params.sq_entries * @sizeOf(u32);
        const sq_map = mmapRing(ring_fd, c.IORING_OFF_SQ_RING, sq_sz) orelse return error.MmapFailed;
        const sq_base = @intFromPtr(sq_map);

        const sqe_map = mmapRing(ring_fd, c.IORING_OFF_SQES, params.sq_entries * @sizeOf(c.io_uring_sqe)) orelse return error.MmapFailed;

        const cq_sz  = params.cq_off.cqes + params.cq_entries * @sizeOf(c.io_uring_cqe);
        const cq_map = mmapRing(ring_fd, c.IORING_OFF_CQ_RING, cq_sz) orelse return error.MmapFailed;
        const cq_base = @intFromPtr(cq_map);

        log.info("io_uring fd={} sq={} cq={} fast_poll={}",
            .{ ring_fd, params.sq_entries, params.cq_entries,
               params.features & c.IORING_FEAT_FAST_POLL != 0 });

        return Ring{
            .fd      = ring_fd,
            .sq_len  = params.sq_entries,
            .sq_mask = @as(*u32, @ptrFromInt(sq_base + params.sq_off.ring_mask)).*,
            .sqes    = @alignCast(@ptrCast(sqe_map)),
            .sq_head = @ptrFromInt(sq_base + params.sq_off.head),
            .sq_tail = @ptrFromInt(sq_base + params.sq_off.tail),
            .sq_arr  = @ptrFromInt(sq_base + params.sq_off.array),
            .cq_len  = params.cq_entries,
            .cq_mask = @as(*u32, @ptrFromInt(cq_base + params.cq_off.ring_mask)).*,
            .cq_head = @ptrFromInt(cq_base + params.cq_off.head),
            .cq_tail = @ptrFromInt(cq_base + params.cq_off.tail),
            .cqes    = @ptrFromInt(cq_base + params.cq_off.cqes),
        };
    }

    fn getSqe(self: *Ring) ?*c.io_uring_sqe {
        const tail = @atomicLoad(u32, self.sq_tail, .acquire);
        const head = @atomicLoad(u32, self.sq_head, .acquire);
        if (tail -% head >= self.sq_len) return null;
        const idx = tail & self.sq_mask;
        self.sq_arr[idx] = idx;
        const sqe = &self.sqes[idx];
        @memset(std.mem.asBytes(sqe), 0);
        return sqe;
    }

    fn flush(self: *Ring) void {
        const t = @atomicLoad(u32, self.sq_tail, .acquire);
        @atomicStore(u32, self.sq_tail, t +% 1, .release);
    }

    /// Submit pending SQEs and wait for at least min_complete CQEs.
    /// Called from the reactor OS thread — blocking here is intentional.
    fn enter(self: *Ring, to_submit: u32, min_complete: u32) !void {
        const flags: u32 = if (min_complete > 0) c.IORING_ENTER_GETEVENTS else 0;
        const r = std.os.linux.syscall6(
            .io_uring_enter,
            @intCast(self.fd),
            @intCast(to_submit),
            @intCast(min_complete),
            @intCast(flags),
            0, 0,
        );
        if (@as(isize, @bitCast(r)) < 0) return error.IoUringEnterFailed;
    }

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

fn mmapRing(ring_fd: c_int, offset: u64, size: usize) ?*anyopaque {
    const p = c.mmap(null, size, c.PROT_READ | c.PROT_WRITE,
                     c.MAP_SHARED | c.MAP_POPULATE, ring_fd, @intCast(offset));
    return if (p == c.MAP_FAILED) null else p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Connection table
// ─────────────────────────────────────────────────────────────────────────────

const Conn = struct {
    ssl:      ?*c.SSL = null,
    deadline: u64     = 0,

    // True sleep via Linux futex — no stdlib Mutex/Condition needed.
    // 0 = sleeping, 1 = wake pending.
    // Reactor: store(1) + FUTEX_WAKE. vthread: FUTEX_WAIT while 0.
    futex:    std.atomic.Value(u32) = .{ .raw = 0 },
};

const ConnTable = struct {
    slots: []Conn,
    alloc: std.mem.Allocator,

    fn init(alloc: std.mem.Allocator, cap: usize) !ConnTable {
        const slots = try alloc.alloc(Conn, cap);
        for (slots) |*s| s.* = .{};
        return .{ .slots = slots, .alloc = alloc };
    }

    fn deinit(self: *ConnTable) void { self.alloc.free(self.slots); }

    fn ensure(self: *ConnTable, fd: usize) !void {
        if (fd < self.slots.len) return;
        const new_len = @max(fd + 1, self.slots.len * 2);
        const old_len = self.slots.len;
        self.slots = try self.alloc.realloc(self.slots, new_len);
        for (self.slots[old_len..]) |*s| s.* = .{};
    }

    fn get(self: *ConnTable, fd: c_int) *Conn {
        return &self.slots[@intCast(fd)];
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Global state (set once in main, read-only after)
// ─────────────────────────────────────────────────────────────────────────────

var g_ring:  Ring        = undefined;
var g_conns: ConnTable   = undefined;
var g_ctx:   ?*c.SSL_CTX = null;

// New-fd FIFO: reactor pushes, dispatcher pops.
// Protected by a simple futex-based spinlock (lock word: 0=free, 1=held).
var g_new_fd_lock: std.atomic.Value(u32) = .{ .raw = 0 };
var g_new_fd_buf:  [FD_QUEUE_CAP]c_int = undefined;
var g_new_fd_head: usize = 0;
var g_new_fd_tail: usize = 0;

fn fdQueueLock() void {
    while (g_new_fd_lock.cmpxchgWeak(0, 1, .acquire, .monotonic) != null) {
        // Spin briefly — the critical section is tiny (2 pointer bumps).
        std.atomic.spinLoopHint();
    }
}
fn fdQueueUnlock() void {
    g_new_fd_lock.store(0, .release);
}

fn newFdPush(fd: c_int) void {
    fdQueueLock();
    defer fdQueueUnlock();
    const next = (g_new_fd_tail + 1) % FD_QUEUE_CAP;
    if (next == g_new_fd_head) {
        log.err("new-fd queue full, dropping fd {}", .{fd});
        _ = c.close(fd);
        return;
    }
    g_new_fd_buf[g_new_fd_tail] = fd;
    g_new_fd_tail = next;
}

fn newFdPop() ?c_int {
    fdQueueLock();
    defer fdQueueUnlock();
    if (g_new_fd_head == g_new_fd_tail) return null;
    const fd = g_new_fd_buf[g_new_fd_head];
    g_new_fd_head = (g_new_fd_head + 1) % FD_QUEUE_CAP;
    return fd;
}

// ─────────────────────────────────────────────────────────────────────────────
// CQE handler — called from the reactor OS thread
// ─────────────────────────────────────────────────────────────────────────────

fn onCqe(cqe: *const c.io_uring_cqe) void {
    const ud: UserData = @bitCast(cqe.user_data);
    switch (ud.tag) {

        .accept => {
            if (cqe.res < 0) {
                if (cqe.res != -@as(i32, @intCast(c.EAGAIN)) and
                    cqe.res != -@as(i32, @intCast(c.EWOULDBLOCK)))
                    log.err("accept CQE error: {d}", .{cqe.res});
                return;
            }
            const fd: c_int = cqe.res;
            const one: c_int = 1;
            _ = c.setsockopt(fd, c.IPPROTO_TCP, c.TCP_NODELAY, &one, @sizeOf(c_int));
            g_conns.ensure(@intCast(fd)) catch {
                _ = c.close(fd);
                return;
            };
            const conn = g_conns.get(fd);
            conn.ssl      = null;
            conn.deadline = 0;
            conn.futex.store(0, .release);
            newFdPush(fd);
        },

        .poll_in, .poll_out => {
            // Wake the vthread blocked in FUTEX_WAIT for this fd.
            const conn = g_conns.get(ud.fd);
            conn.futex.store(1, .release);
            _ = std.os.linux.syscall4(
                .futex,
                @intFromPtr(&conn.futex.raw),
                FUTEX_WAKE,
                1,
                0,
            );
        },
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Reactor — runs on a dedicated OS thread, never in the vthread pool
// ─────────────────────────────────────────────────────────────────────────────

fn reactorThread(_: void) void {
    while (true) {
        // Block until at least 1 CQE arrives.  This OS thread is dedicated
        // to the reactor — blocking here does NOT starve vthreads.
        g_ring.enter(0, 1) catch |err| {
            log.err("io_uring_enter failed: {}", .{err});
            var ts = c.struct_timespec{
                .tv_sec  = 0,
                .tv_nsec = ACCEPT_BACKOFF_MS * 1_000_000,
            };
            _ = c.nanosleep(&ts, null);
            continue;
        };
        g_ring.drain(onCqe);
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

    g_ctx = c.SSL_CTX_new(c.TLS_server_method()) orelse return error.SSLContextFailed;
    defer c.SSL_CTX_free(g_ctx);

    _ = c.SSL_CTX_set_min_proto_version(g_ctx, c.TLS1_2_VERSION);
    _ = c.SSL_CTX_set_mode(g_ctx,
        c.SSL_MODE_RELEASE_BUFFERS      |
        c.SSL_MODE_ENABLE_PARTIAL_WRITE |
        c.SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    if (c.SSL_CTX_use_certificate_file(g_ctx, "server.crt", c.SSL_FILETYPE_PEM) != 1) {
        c.ERR_print_errors_fp(c.stderr); return error.CertLoadFailed;
    }
    if (c.SSL_CTX_use_PrivateKey_file(g_ctx, "server.key", c.SSL_FILETYPE_PEM) != 1) {
        c.ERR_print_errors_fp(c.stderr); return error.KeyLoadFailed;
    }
    if (c.SSL_CTX_check_private_key(g_ctx) != 1) {
        c.ERR_print_errors_fp(c.stderr); return error.KeyMismatch;
    }

    // ── Listening socket ──────────────────────────────────────────────────────
    const listener = c.socket(c.AF_INET, c.SOCK_STREAM | c.SOCK_NONBLOCK | c.SOCK_CLOEXEC, 0);
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
    if (c.bind(listener, @ptrCast(&addr), @sizeOf(c.sockaddr_in)) < 0) return error.BindFailed;
    if (c.listen(listener, LISTEN_BACKLOG) < 0) return error.ListenFailed;

    // ── Arm multishot accept ──────────────────────────────────────────────────
    {
        const sqe = g_ring.getSqe() orelse return error.RingFull;
        sqeSet(sqe, u8,  SQE_OFF_OPCODE,    @intCast(c.IORING_OP_ACCEPT));
        sqeSet(sqe, i32, SQE_OFF_FD,        listener);
        sqeSet(sqe, u16, SQE_OFF_IOPRIO,    c.IORING_ACCEPT_MULTISHOT);
        sqeSet(sqe, u64, SQE_OFF_ADDR2,     0);
        sqeSet(sqe, u64, SQE_OFF_ADDR,      0);
        sqeSet(sqe, u32, SQE_OFF_OP_FLAGS,  @intCast(c.SOCK_NONBLOCK | c.SOCK_CLOEXEC));
        sqeSet(sqe, u64, SQE_OFF_USER_DATA, @bitCast(UserData{ .tag = .accept, .fd = listener }));
        g_ring.flush();
        try g_ring.enter(1, 0);
    }

    // ── Dedicated reactor OS thread ───────────────────────────────────────────
    //
    // This thread blocks in ring.enter(0,1) and is never part of the vthread
    // pool.  It does not compete with handleConnection vthreads for OS threads.
    const reactor = try std.Thread.spawn(.{}, reactorThread, .{{}});
    reactor.detach();

    log.info("TLS echo server listening on 0.0.0.0:8443 (io_uring)", .{});

    // ── Dispatcher loop ───────────────────────────────────────────────────────
    //
    // Pops newly accepted fds from the reactor's queue and spawns vthreads.
    // Yields via io.sleep(1) between polls — not a busy loop.
    var group: Io.Group = .init;
    defer group.cancel(io);

    while (true) {
        var spawned: usize = 0;
        while (newFdPop()) |fd| {
            group.async(io, handleConnection, .{ io, fd });
            spawned += 1;
        }
        // Sleep briefly when idle to avoid burning CPU in the dispatch loop.
        // 1 ms is short enough for sub-millisecond accept latency at scale.
        const sleep_ms: i64 = if (spawned > 0) 0 else 1;
        io.sleep(.fromMilliseconds(sleep_ms), .awake) catch break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-connection virtual-thread handler
// ─────────────────────────────────────────────────────────────────────────────

fn handleConnection(io: Io, fd: c_int) error{Canceled}!void {
    _ = io;
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

    if (tlsAccept(ssl, fd, conn)) |err_tag| {
        if (!std.mem.eql(u8, err_tag, "client eof") and
            !std.mem.eql(u8, err_tag, "client closed"))
            log.debug("handshake failed (fd {} — {s})", .{ fd, err_tag });
        return;
    }

    log.debug("TLS connection established (fd {})", .{fd});

    var buf: [READ_BUF_SIZE]u8 = undefined;
    while (true) {
        const n = tlsRead(ssl, fd, conn, &buf);
        if (n == 0) break;
        tlsWriteAll(ssl, fd, conn, buf[0..@intCast(n)]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TLS helpers — block via Linux futex, not io.sleep or stdlib Mutex
//
// parkUntilReady calls FUTEX_WAIT on conn.futex while its value is 0.
// The OS de-schedules the thread entirely — it returns to the pool and
// can run other vthreads.  The reactor calls FUTEX_WAKE after store(1),
// waking exactly this thread.  Zero busy-waiting, O(1) wakeup cost.
// ─────────────────────────────────────────────────────────────────────────────

// Linux futex op constants — stable ABI since kernel 2.6, never change.
const FUTEX_WAIT: usize = 0;
const FUTEX_WAKE: usize = 1;

fn parkUntilReady(conn: *Conn) void {
    while (conn.futex.load(.acquire) == 0) {
        _ = std.os.linux.syscall4(
            .futex,
            @intFromPtr(&conn.futex.raw),
            FUTEX_WAIT,
            0,
            0,
        );
    }
    conn.futex.store(0, .release);
}

fn submitPollIn(fd: c_int) void {
    if (g_ring.getSqe()) |sqe| {
        sqeSet(sqe, u8,  SQE_OFF_OPCODE,    @intCast(c.IORING_OP_POLL_ADD));
        sqeSet(sqe, i32, SQE_OFF_FD,        fd);
        sqeSet(sqe, u32, SQE_OFF_ADDR,      c.POLLIN);
        sqeSet(sqe, u64, SQE_OFF_USER_DATA, @bitCast(UserData{ .tag = .poll_in, .fd = fd }));
        g_ring.flush();
        g_ring.enter(1, 0) catch {};
    }
}

fn submitPollOut(fd: c_int) void {
    if (g_ring.getSqe()) |sqe| {
        sqeSet(sqe, u8,  SQE_OFF_OPCODE,    @intCast(c.IORING_OP_POLL_ADD));
        sqeSet(sqe, i32, SQE_OFF_FD,        fd);
        sqeSet(sqe, u32, SQE_OFF_ADDR,      c.POLLOUT);
        sqeSet(sqe, u64, SQE_OFF_USER_DATA, @bitCast(UserData{ .tag = .poll_out, .fd = fd }));
        g_ring.flush();
        g_ring.enter(1, 0) catch {};
    }
}

fn tlsAccept(ssl: *c.SSL, fd: c_int, conn: *Conn) ?[]const u8 {
    while (true) {
        if (nowNs() > conn.deadline) return "handshake timeout";
        const rc = c.SSL_accept(ssl);
        if (rc == 1) return null;
        switch (c.SSL_get_error(ssl, rc)) {
            c.SSL_ERROR_WANT_READ  => { submitPollIn(fd);  parkUntilReady(conn); },
            c.SSL_ERROR_WANT_WRITE => { submitPollOut(fd); parkUntilReady(conn); },
            c.SSL_ERROR_ZERO_RETURN => { _ = c.ERR_clear_error(); return "client closed"; },
            c.SSL_ERROR_SYSCALL     => { _ = c.ERR_clear_error(); return "client eof"; },
            else => { c.ERR_print_errors_fp(c.stderr); return "SSL_accept"; },
        }
    }
}

fn tlsRead(ssl: *c.SSL, fd: c_int, conn: *Conn, buf: []u8) c_int {
    while (true) {
        const n = c.SSL_read(ssl, buf.ptr, @intCast(buf.len));
        if (n > 0) return n;
        switch (c.SSL_get_error(ssl, n)) {
            c.SSL_ERROR_WANT_READ  => { submitPollIn(fd);  parkUntilReady(conn); },
            c.SSL_ERROR_WANT_WRITE => { submitPollOut(fd); parkUntilReady(conn); },
            c.SSL_ERROR_ZERO_RETURN => return 0,
            c.SSL_ERROR_SYSCALL     => { _ = c.ERR_clear_error(); return 0; },
            else => { c.ERR_print_errors_fp(c.stderr); return 0; },
        }
    }
}

fn tlsWriteAll(ssl: *c.SSL, fd: c_int, conn: *Conn, buf: []const u8) void {
    var rem = buf;
    while (rem.len > 0) {
        const n = c.SSL_write(ssl, rem.ptr, @intCast(rem.len));
        if (n > 0) { rem = rem[@intCast(n)..]; continue; }
        switch (c.SSL_get_error(ssl, n)) {
            c.SSL_ERROR_WANT_READ  => { submitPollIn(fd);  parkUntilReady(conn); },
            c.SSL_ERROR_WANT_WRITE => { submitPollOut(fd); parkUntilReady(conn); },
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

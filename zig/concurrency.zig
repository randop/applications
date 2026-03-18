//! concurrency.zig — Fearless Concurrency for Zig 0.16.0-dev
//!
//! Demonstrates Zig's std.Io async/await model — virtual (green) threads
//! that look like normal blocking code but never hog an OS thread while
//! waiting.  Architecturally similar to Java 25 Project Loom.
//!
//! Build / run:
//!   zig build-exe concurrency.zig && ./concurrency
//!   zig run concurrency.zig
//!
//! Key APIs (0.16.0-dev / early 2026 master)
//!   std.Io.Threaded          — virtual-thread runtime (OS thread pool +
//!                              cooperative stack-switching / green threads)
//!   io.async(fn, args)       — spawn a virtual thread  (≈ Thread.ofVirtual().start())
//!   future.await(io)         — join, returns the fn's return value
//!   future.cancel(io)        — request cancellation *and* join (safe in defer)
//!   std.Io.Group             — fire-and-forget fan-out for Cancelable!void tasks
//!   group.async(io, fn, args)— enqueue a task in the group
//!   group.await(io)          — wait for all group tasks → Cancelable!void
//!   group.cancel(io)         — cancel remaining group tasks → void
//!   io.sleep(duration, wake) — cooperative sleep (yields the OS thread)
//!   std.Io.Mutex             — cooperative mutex; lock(io) yields instead of
//!                              blocking an OS thread
//!
//! Breaking changes fixed in this file vs. earlier dev builds:
//!   • main() must accept `std.process.Init.Minimal` ("juicy main", PR #30644)
//!   • Threaded.init(gpa, .{ .environ = init.environ }) — 2 args required
//!   • Duration.fromMilliseconds(x: i64) — parameter is signed, not unsigned
//!   • group.await() and group.cancel() take NO arguments
//!   • Tasks passed to group.async() must return `Cancelable!void`
//!     (only error.Canceled is allowed; use plain io.async for richer errors)

const std = @import("std");
const Io = std.Io;
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

// ─────────────────────────────────────────────────────────────────────────────
// Entry point — "juicy main" signature required since PR #30644.
// ─────────────────────────────────────────────────────────────────────────────

pub fn main(init: std.process.Init.Minimal) !void {
    var debug_allocator: std.heap.DebugAllocator(.{}) = .init;
    defer assert(debug_allocator.deinit() == .ok);
    const gpa = debug_allocator.allocator();

    // std.Io.Threaded: the virtual-thread runtime.
    // Multiplexes lightweight tasks over an OS thread pool via stack-switching.
    // InitOptions.environ is mandatory since PR #30644.
    var threaded: Io.Threaded = .init(gpa, .{ .environ = init.environ });
    defer threaded.deinit();
    const io = threaded.io();

    const sep = "─" ** 60;

    std.debug.print("\n{s}\n", .{sep});
    try demo_basic_async_await(gpa, io);

    std.debug.print("\n{s}\n", .{sep});
    try demo_fan_out(gpa, io);

    std.debug.print("\n{s}\n", .{sep});
    try demo_cancel_on_error(gpa, io);

    std.debug.print("\n{s}\n", .{sep});
    try demo_group_fan_out(gpa, io);

    std.debug.print("\n{s}\n", .{sep});
    try demo_mutex_shared_state(gpa, io);

    std.debug.print("\n{s}\n", .{sep});
    try demo_producer_consumer(gpa, io);

    std.debug.print("\n{s}\nAll demos passed ✓\n", .{sep});
}

// ─────────────────────────────────────────────────────────────────────────────
// [1] Basic async / await
//
// io.async(fn, args) → Future(T)   spawns a virtual thread
// future.await(io)   → T           joins it cooperatively
// ─────────────────────────────────────────────────────────────────────────────

fn demo_basic_async_await(gpa: Allocator, io: Io) !void {
    _ = gpa;
    std.debug.print("[1] Basic async/await\n", .{});

    var task = io.async(slow_hello, .{ io, "world" });

    std.debug.print("    main: doing other work while task runs…\n", .{});
    // Duration.fromMilliseconds takes i64 — use a signed literal.
    io.sleep(.fromMilliseconds(200), .awake) catch {};

    task.await(io);
    std.debug.print("    main: task joined\n", .{});
}

fn slow_hello(io: Io, name: []const u8) void {
    io.sleep(.fromMilliseconds(400), .awake) catch {};
    std.debug.print("    hello, {s}!\n", .{name});
}

// ─────────────────────────────────────────────────────────────────────────────
// [2] Fan-out — N tasks, all spawned before any await
//
// Wall-clock time ≈ max(durations), not sum.
// For tasks with arbitrary error sets, use plain io.async + defer cancel.
// ─────────────────────────────────────────────────────────────────────────────

const WORKERS = 6;

fn demo_fan_out(gpa: Allocator, io: Io) !void {
    _ = gpa;
    std.debug.print("[2] Fan-out: {d} concurrent tasks\n", .{WORKERS});

    var futures: [WORKERS]Io.Future(void) = undefined;
    for (&futures, 0..) |*f, i| {
        f.* = io.async(worker_task, .{ io, i });
    }
    for (&futures) |*f| f.await(io);

    std.debug.print("    all {d} workers finished\n", .{WORKERS});
}

fn worker_task(io: Io, id: usize) void {
    // Cast usize → i64 for Duration.fromMilliseconds.
    const ms: i64 = @intCast(50 * (id + 1));
    io.sleep(.fromMilliseconds(ms), .awake) catch {};
    std.debug.print("    worker {d} done (slept {d} ms)\n", .{ id, ms });
}

// ─────────────────────────────────────────────────────────────────────────────
// [3] Fearless error propagation with defer cancel
//
// Recommended pattern (from Andrew Kelley / Zigtoberfest 2025 + Ziggit 2026):
//   defer future.cancel(io) catch {}   on every future, unconditionally.
//
// • If `try future.await(io)` returns an error and skips later awaits, the
//   deferred cancels still join every spawned task — nothing leaks.
// • cancel() and await() are idempotent w.r.t. each other.
// ─────────────────────────────────────────────────────────────────────────────

const WorkError = error{BadInput};

fn demo_cancel_on_error(gpa: Allocator, io: Io) !void {
    _ = gpa;
    std.debug.print("[3] Cancel-on-error (safe defer pattern)\n", .{});

    var task_a = io.async(failable_task, .{ io, false }); // succeeds
    defer task_a.cancel(io) catch {};                      // no-op after await

    var task_b = io.async(failable_task, .{ io, true });  // fails
    defer task_b.cancel(io) catch {};                      // joins even if try fires

    try task_a.await(io);
    task_b.await(io) catch |e| {
        std.debug.print("    task_b failed (expected): {}\n", .{e});
    };

    std.debug.print("    both futures joined without leaks\n", .{});
}

fn failable_task(io: Io, should_fail: bool) WorkError!void {
    io.sleep(.fromMilliseconds(80), .awake) catch {};
    if (should_fail) return WorkError.BadInput;
}

// ─────────────────────────────────────────────────────────────────────────────
// [4] std.Io.Group — fire-and-forget fan-out
//
// Group is designed for tasks that return `Cancelable!void`, i.e. only
// `error.Canceled` or void — not arbitrary error sets.
//
// API (both await and cancel take an `io` argument):
//   group.async(io, fn, args)  — enqueue
//   try group.await(io)        — wait for all → Cancelable!void
//   group.cancel(io)           — cancel remaining (returns void, safe in defer)
//
// For tasks with richer error sets, use plain io.async + defer cancel (demo [3]).
// ─────────────────────────────────────────────────────────────────────────────

fn demo_group_fan_out(gpa: Allocator, io: Io) !void {
    _ = gpa;
    std.debug.print("[4] std.Io.Group fire-and-forget fan-out\n", .{});

    var group: std.Io.Group = .init;
    defer group.cancel(io); // joins all tasks on scope exit

    group.async(io, group_task, .{ io, "alpha" });
    group.async(io, group_task, .{ io, "beta" });
    group.async(io, group_task, .{ io, "gamma" });

    try group.await(io); // waits for all enqueued tasks → Cancelable!void
    std.debug.print("    all group tasks completed\n", .{});
}

// Tasks passed to group.async MUST return `error.Canceled!void`
// (i.e. only error.Canceled, no other errors).
fn group_task(io: Io, name: []const u8) error{Canceled}!void {
    io.sleep(.fromMilliseconds(100), .awake) catch |e| switch (e) {
        error.Canceled => return error.Canceled,
    };
    std.debug.print("    group task finished: {s}\n", .{name});
}

// ─────────────────────────────────────────────────────────────────────────────
// [5] Shared state with std.Io.Mutex
//
// std.Io.Mutex is a *cooperative* mutex: lock(io) yields the virtual thread
// instead of blocking an OS thread, so the thread pool is never starved.
// ─────────────────────────────────────────────────────────────────────────────

const COUNTER_TASKS = 20;

fn demo_mutex_shared_state(gpa: Allocator, io: Io) !void {
    _ = gpa;
    std.debug.print("[5] Shared counter with Io.Mutex ({d} virtual threads)\n", .{COUNTER_TASKS});

    var mutex: std.Io.Mutex = .{ .state = .{ .raw = .unlocked } };
    var counter: u64 = 0;

    var futures: [COUNTER_TASKS]Io.Future(error{Canceled}!void) = undefined;
    for (&futures) |*f| {
        f.* = io.async(increment_counter, .{ io, &mutex, &counter });
    }
    for (&futures) |*f| f.await(io) catch {};

    assert(counter == COUNTER_TASKS);
    std.debug.print("    counter = {d} (expected {d}) ✓\n", .{ counter, COUNTER_TASKS });
}

fn increment_counter(io: Io, mutex: *std.Io.Mutex, counter: *u64) error{Canceled}!void {
    try mutex.lock(io);
    defer mutex.unlock(io);
    io.sleep(.fromMilliseconds(1), .awake) catch {};
    counter.* += 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// [6] Producer / consumer via a bounded ring buffer
//
// Idiomatic Zig message-passing between virtual threads:
//   • Ring buffer protected by std.Thread.Mutex.
//   • Producer yields (io.sleep) when full; consumer yields when empty.
//   • Cooperative sleep = backpressure, no OS thread ever blocks.
//
// Java analogue: ArrayBlockingQueue used across virtual threads.
// ─────────────────────────────────────────────────────────────────────────────

const RING_CAP = 4;
const PIPE_ITEMS = 10;

const RingBuf = struct {
    buf: [RING_CAP]u64 = undefined,
    head: usize = 0,
    tail: usize = 0,
    count: usize = 0,
    done: bool = false,
    mutex: std.Io.Mutex = .{ .state = .{ .raw = .unlocked } },

    fn push(self: *RingBuf, io: Io, val: u64) error{Canceled}!void {
        while (true) {
            try self.mutex.lock(io);
            if (self.count < RING_CAP) {
                self.buf[self.tail] = val;
                self.tail = (self.tail + 1) % RING_CAP;
                self.count += 1;
                self.mutex.unlock(io);
                return;
            }
            self.mutex.unlock(io);
            io.sleep(.fromMilliseconds(5), .awake) catch {}; // backpressure yield
        }
    }

    fn pop(self: *RingBuf, io: Io) error{Canceled}!?u64 {
        while (true) {
            try self.mutex.lock(io);
            if (self.count > 0) {
                const val = self.buf[self.head];
                self.head = (self.head + 1) % RING_CAP;
                self.count -= 1;
                self.mutex.unlock(io);
                return val;
            }
            if (self.done) {
                self.mutex.unlock(io);
                return null;
            }
            self.mutex.unlock(io);
            io.sleep(.fromMilliseconds(5), .awake) catch {}; // idle yield
        }
    }

    fn finish(self: *RingBuf, io: Io) error{Canceled}!void {
        try self.mutex.lock(io);
        defer self.mutex.unlock(io);
        self.done = true;
    }
};

fn demo_producer_consumer(gpa: Allocator, io: Io) !void {
    _ = gpa;
    std.debug.print("[6] Producer / consumer (ring buffer, {d} items)\n", .{PIPE_ITEMS});

    var ring: RingBuf = .{};

    var prod = io.async(producer, .{ io, &ring });
    defer prod.cancel(io) catch {};

    var cons = io.async(consumer, .{ io, &ring });
    defer cons.cancel(io) catch {};

    try prod.await(io);
    try cons.await(io);

    std.debug.print("    pipeline complete\n", .{});
}

fn producer(io: Io, ring: *RingBuf) !void {
    for (0..PIPE_ITEMS) |i| {
        io.sleep(.fromMilliseconds(25), .awake) catch {};
        try ring.push(io, @intCast(i));
        std.debug.print("    produced {d}\n", .{i});
    }
    try ring.finish(io);
}

fn consumer(io: Io, ring: *RingBuf) !void {
    var sum: u64 = 0;
    while (try ring.pop(io)) |item| {
        std.debug.print("    consumed {d}\n", .{item});
        sum += item;
        io.sleep(.fromMilliseconds(40), .awake) catch {};
    }
    const expected: u64 = PIPE_ITEMS * (PIPE_ITEMS - 1) / 2;
    assert(sum == expected);
    std.debug.print("    sum = {d} ✓\n", .{sum});
}

// ─────────────────────────────────────────────────────────────────────────────
// API cheat-sheet — Java 25 virtual threads ↔ Zig 0.16.0-dev
//
//  Java                                Zig
//  ─────────────────────────────────── ──────────────────────────────────────
//  Thread.ofVirtual().start(r)         io.async(fn, args) → Future(T)
//  thread.join()                       future.await(io)   → T (or error)
//  thread.interrupt()                  future.cancel(io)  — also joins
//  StructuredTaskScope.fork(task)      group.async(io, fn, args)
//  scope.join()                        try group.await(io)
//  scope.shutdown() / .close()         group.cancel(io)  (in defer, returns void)
//  ReentrantLock / synchronized        std.Io.Mutex — lock(io) / unlock(io)
//  ArrayBlockingQueue                  RingBuf + Mutex + io.sleep (demo [6])
//  Thread.sleep(n)                     io.sleep(.fromMilliseconds(n), .awake)
//  virtual thread executor pool        std.Io.Threaded (the runtime itself)
// ─────────────────────────────────────────────────────────────────────────────

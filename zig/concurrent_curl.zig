//! concurrent_curl.zig — Fearless Concurrency + libcurl for Zig 0.16.0-dev
//!
//! Spawns N virtual threads, each performing an independent HTTP GET via
//! libcurl, then aggregates the results safely with std.Io.Mutex.
//!
//! Build:
//!   zig build-exe concurrent_curl.zig \
//!       --library curl --library c \
//!       $(pkg-config --libs libcurl)
//!
//! Design overview
//! ───────────────
//!   • std.Io.Threaded        — green-thread runtime (OS pool + stack-switching)
//!   • io.async / future.await — spawn + join virtual threads
//!   • std.Io.Group            — fire-and-forget fan-out of Cancelable!void tasks
//!   • std.Io.Mutex            — cooperative mutex; never starves the OS thread pool
//!   • libcurl easy API        — one handle per virtual thread (not thread-safe to share)
//!   • curl_global_init called once in main; each thread owns its easy handle
//!
//! Notes on curl + virtual threads
//! ────────────────────────────────
//!   curl_easy_perform() is a *blocking* C call — it will park the OS thread for
//!   the duration of the transfer.  With std.Io.Threaded the scheduler cannot
//!   preempt blocking C code, so each in-flight request consumes one real OS thread
//!   from the pool.  Set the pool size ≥ MAX_CONCURRENT_REQUESTS (or use the curl
//!   multi interface for truly non-blocking I/O).  For a quick demo this is fine.

const std = @import("std");
const Io = std.Io;
const Allocator = std.mem.Allocator;
const assert = std.debug.assert;

const cURL = @cImport({
    @cInclude("curl/curl.h");
});

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

const URLS = [_][]const u8{
    "https://httpbin.org/get",
    "https://httpbin.org/uuid",
    "https://httpbin.org/ip",
    "https://httpbin.org/headers",
    "https://httpbin.org/user-agent",
};

const MAX_CONCURRENT_REQUESTS = URLS.len;

// ─────────────────────────────────────────────────────────────────────────────
// Result type — each virtual thread fills one of these
// ─────────────────────────────────────────────────────────────────────────────

const FetchResult = struct {
    url: []const u8,
    status: enum { ok, err },
    bytes: usize,
    body_preview: [128]u8 = undefined, // first N bytes, for display
    preview_len: usize = 0,
    err_msg: [128]u8 = undefined,
    err_len: usize = 0,
};

// ─────────────────────────────────────────────────────────────────────────────
// Shared aggregator — protected by std.Io.Mutex
// ─────────────────────────────────────────────────────────────────────────────

const Aggregator = struct {
    mutex: std.Io.Mutex = .{ .state = .{ .raw = .unlocked } },
    // 0.16.0-dev: ArrayList no longer stores its allocator internally.
    // We keep a copy here so deinit and record can pass it through.
    alloc: Allocator,
    results: std.ArrayList(FetchResult),
    total_bytes: usize = 0,

    fn init(alloc: Allocator) Aggregator {
        // .{} is the new zero-init for an unowned ArrayList.
        // 0.16.0-dev ArrayList has no default values — must supply both fields.
        return .{ .alloc = alloc, .results = .{ .items = &.{}, .capacity = 0 } };
    }

    fn deinit(self: *Aggregator) void {
        self.results.deinit(self.alloc);
    }

    /// Called from any virtual thread — acquires mutex before mutating state.
    fn record(self: *Aggregator, io: Io, result: FetchResult) error{Canceled}!void {
        try self.mutex.lock(io);
        defer self.mutex.unlock(io);

        // append now requires the allocator explicitly.
        self.results.append(self.alloc, result) catch {};
        if (result.status == .ok) self.total_bytes += result.bytes;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// curl write callback — appends chunks into an ArrayList(u8)
//
// 0.16.0-dev: ArrayList.appendSlice requires an explicit Allocator.
// We pass a small wrapper struct through CURLOPT_WRITEDATA so the callback
// has both the list and the allocator without a global variable.
// ─────────────────────────────────────────────────────────────────────────────

const BodyBuf = struct {
    alloc: Allocator,
    list: std.ArrayList(u8),
};

fn writeCallback(
    data: *anyopaque,
    size: c_uint,
    nmemb: c_uint,
    user_data: *anyopaque,
) callconv(.c) c_uint {
    const buf: *BodyBuf = @ptrCast(@alignCast(user_data));
    const byte_count = size * nmemb;
    if (byte_count / size != nmemb) return 0; // overflow guard
    const slice = @as([*]const u8, @ptrCast(data))[0..byte_count];
    buf.list.appendSlice(buf.alloc, slice) catch return 0;
    return byte_count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-request worker — runs in its own virtual thread
// ─────────────────────────────────────────────────────────────────────────────

const WorkerArgs = struct {
    io: Io,
    alloc: Allocator,
    url: []const u8,
    agg: *Aggregator,
    id: usize,
};

fn fetch_worker(args: WorkerArgs) error{Canceled}!void {
    const io = args.io;
    var result: FetchResult = .{
        .url = args.url,
        .status = .err,
        .bytes = 0,
    };

    // Each virtual thread gets its own easy handle — curl handles are not
    // thread-safe to share, but creating one per task is cheap.
    const handle = cURL.curl_easy_init() orelse {
        const msg = "curl_easy_init failed";
        @memcpy(result.err_msg[0..msg.len], msg);
        result.err_len = msg.len;
        try args.agg.record(io, result);
        return;
    };
    defer cURL.curl_easy_cleanup(handle);

    var body: BodyBuf = .{ .alloc = args.alloc, .list = .{ .items = &.{}, .capacity = 0 } };
    defer body.list.deinit(args.alloc);

    // Configure the easy handle
    const ok = cURL.CURLE_OK;
    if (cURL.curl_easy_setopt(handle, cURL.CURLOPT_URL, args.url.ptr) != ok or
        cURL.curl_easy_setopt(handle, cURL.CURLOPT_WRITEFUNCTION, &writeCallback) != ok or
        cURL.curl_easy_setopt(handle, cURL.CURLOPT_WRITEDATA, &body) != ok or
        // Reasonable timeouts so slow URLs don't hang the demo forever
        cURL.curl_easy_setopt(handle, cURL.CURLOPT_TIMEOUT_MS, @as(c_long, 8_000)) != ok or
        cURL.curl_easy_setopt(handle, cURL.CURLOPT_CONNECTTIMEOUT_MS, @as(c_long, 4_000)) != ok)
    {
        const msg = "curl_easy_setopt failed";
        @memcpy(result.err_msg[0..msg.len], msg);
        result.err_len = msg.len;
        try args.agg.record(io, result);
        return;
    }

    std.debug.print("  [{d}] → fetching {s}\n", .{ args.id, args.url });

    // curl_easy_perform blocks the OS thread for the duration of the transfer.
    // This is expected — see the module-level note about curl + virtual threads.
    const rc = cURL.curl_easy_perform(handle);

    if (rc != ok) {
        const c_msg = cURL.curl_easy_strerror(rc);
        const msg = std.mem.span(c_msg);
        const copy_len = @min(msg.len, result.err_msg.len);
        @memcpy(result.err_msg[0..copy_len], msg[0..copy_len]);
        result.err_len = copy_len;
        try args.agg.record(io, result);
        return;
    }

    // Success — fill result
    result.status = .ok;
    result.bytes = body.list.items.len;

    const preview_len = @min(body.list.items.len, result.body_preview.len);
    @memcpy(result.body_preview[0..preview_len], body.list.items[0..preview_len]);
    result.preview_len = preview_len;

    std.debug.print("  [{d}] ✓ {s} — {d} bytes\n", .{ args.id, args.url, result.bytes });
    try args.agg.record(io, result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Wrapper that matches the group.async signature: (Io, …) → error{Canceled}!void
// We store the full WorkerArgs in a struct so group.async can forward `io`.
// ─────────────────────────────────────────────────────────────────────────────

fn group_fetch(io: Io, args: WorkerArgs) error{Canceled}!void {
    _ = io; // already embedded in args.io
    try fetch_worker(args);
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 1 — io.async fan-out (arbitrary error sets, defer cancel pattern)
// ─────────────────────────────────────────────────────────────────────────────

fn demo_fanout_async(gpa: Allocator, io: Io, agg: *Aggregator) !void {
    std.debug.print("\n[1] Fan-out with io.async + defer cancel (arbitrary errors)\n", .{});

    // Spawn one future per URL before awaiting any of them.
    var futures: [MAX_CONCURRENT_REQUESTS]Io.Future(error{Canceled}!void) = undefined;
    for (&futures, URLS, 0..) |*f, url, i| {
        const args: WorkerArgs = .{
            .io = io,
            .alloc = gpa,
            .url = url,
            .agg = agg,
            .id = i,
        };
        f.* = io.async(fetch_worker, .{args});
        defer f.cancel(io) catch {}; // joins on scope exit regardless of errors
    }

    // Await all — wall-clock time ≈ slowest request, not sum of all.
    for (&futures) |*f| f.await(io) catch {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo 2 — std.Io.Group fire-and-forget fan-out
// ─────────────────────────────────────────────────────────────────────────────

fn demo_group_fanout(gpa: Allocator, io: Io, agg: *Aggregator) !void {
    std.debug.print("\n[2] Fan-out with std.Io.Group (fire-and-forget)\n", .{});

    var group: std.Io.Group = .init;
    defer group.cancel(io); // joins survivors on scope exit

    for (URLS, 0..) |url, i| {
        const args: WorkerArgs = .{
            .io = io,
            .alloc = gpa,
            .url = url,
            .agg = agg,
            .id = i + MAX_CONCURRENT_REQUESTS, // distinct IDs from demo 1
        };
        group.async(io, group_fetch, .{ io, args });
    }

    try group.await(io); // waits for all → Cancelable!void
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry point — "juicy main" (PR #30644)
// ─────────────────────────────────────────────────────────────────────────────

pub fn main(init: std.process.Init.Minimal) !void {
    var debug_alloc: std.heap.DebugAllocator(.{}) = .init;
    defer assert(debug_alloc.deinit() == .ok);
    const gpa = debug_alloc.allocator();

    // One-time libcurl global init — must happen before any easy handle use.
    if (cURL.curl_global_init(cURL.CURL_GLOBAL_ALL) != cURL.CURLE_OK)
        return error.CURLGlobalInitFailed;
    defer cURL.curl_global_cleanup();

    // Virtual-thread runtime.  Thread pool size defaults to nCPU; bump it to
    // at least MAX_CONCURRENT_REQUESTS so blocking curl calls don't deadlock.
    var threaded: Io.Threaded = .init(gpa, .{
        .environ = init.environ,
        // Uncomment if your build exposes thread_count in InitOptions:
        // .thread_count = MAX_CONCURRENT_REQUESTS,
    });
    defer threaded.deinit();
    const io = threaded.io();

    var agg = Aggregator.init(gpa);
    defer agg.deinit();

    const sep = "─" ** 60;
    std.debug.print("{s}\nconcurrent_curl — Zig 0.16.0-dev\n{s}\n", .{ sep, sep });

    // ── Demo 1: plain io.async fan-out ───────────────────────────────────────
    try demo_fanout_async(gpa, io, &agg);

    // ── Demo 2: std.Io.Group fan-out ─────────────────────────────────────────
    try demo_group_fanout(gpa, io, &agg);

    // ── Summary ──────────────────────────────────────────────────────────────
    std.debug.print("\n{s}\nSummary ({d} results)\n{s}\n", .{ sep, agg.results.items.len, sep });

    var ok_count: usize = 0;
    var err_count: usize = 0;

    for (agg.results.items) |r| {
        switch (r.status) {
            .ok => {
                ok_count += 1;
                // Print a sanitised single-line preview (strip newlines)
                var preview: [128]u8 = undefined;
                var plen: usize = 0;
                for (r.body_preview[0..r.preview_len]) |ch| {
                    if (ch == '\n' or ch == '\r') {
                        preview[plen] = ' ';
                    } else {
                        preview[plen] = ch;
                    }
                    plen += 1;
                    if (plen >= preview.len) break;
                }
                std.debug.print("  ✓ {s}\n    {d} bytes | preview: {s}…\n", .{
                    r.url, r.bytes, preview[0..plen],
                });
            },
            .err => {
                err_count += 1;
                std.debug.print("  ✗ {s}\n    error: {s}\n", .{
                    r.url, r.err_msg[0..r.err_len],
                });
            },
        }
    }

    std.debug.print("{s}\nDone: {d} ok, {d} errors, {d} total bytes\n{s}\n", .{
        sep, ok_count, err_count, agg.total_bytes, sep,
    });
}

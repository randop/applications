//```sh
// zig version
//0.16.0-dev.2905+5d71e3051
//
// zig build-exe curl.zig --library curl --library c $(pkg-config --libs libcurl)
//```

const std = @import("std");
const cURL = @cImport({
    @cInclude("curl/curl.h");
});

pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.c_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();

    // global curl init, or fail
    if (cURL.curl_global_init(cURL.CURL_GLOBAL_ALL) != cURL.CURLE_OK)
        return error.CURLGlobalInitFailed;
    defer cURL.curl_global_cleanup();

    // curl easy handle init, or fail
    const handle = cURL.curl_easy_init() orelse return error.CURLHandleInitFailed;
    defer cURL.curl_easy_cleanup(handle);

    var response_buffer = std.array_list.Managed(u8).init(allocator);
    // superfluous when using an arena allocator, but
    // important if the allocator implementation changes
    defer response_buffer.deinit();

    // setup curl options
    if (cURL.curl_easy_setopt(handle, cURL.CURLOPT_URL, "https://www.quizbin.com") != cURL.CURLE_OK)
        return error.CouldNotSetURL;

    // set write function callbacks
    if (cURL.curl_easy_setopt(handle, cURL.CURLOPT_WRITEFUNCTION, writeToArrayListCallback) != cURL.CURLE_OK)
        return error.CouldNotSetWriteCallback;
    if (cURL.curl_easy_setopt(handle, cURL.CURLOPT_WRITEDATA, &response_buffer) != cURL.CURLE_OK)
        return error.CouldNotSetWriteCallback;

    // perform
    if (cURL.curl_easy_perform(handle) != cURL.CURLE_OK)
        return error.FailedToPerformRequest;

    std.log.info("Got response of {d} bytes", .{response_buffer.items.len});
    std.debug.print("{s}\n", .{response_buffer.items});
}

fn writeToArrayListCallback(
    data: *anyopaque,
    size: c_uint,
    nmemb: c_uint,
    user_data: *anyopaque,
) callconv(.c) c_uint {
    const buffer: *std.array_list.Managed(u8) = @ptrCast(@alignCast(user_data));

    const byte_count = size * nmemb;
    // Safety: prevent overflow (rare with curl, but good practice)
    if (byte_count / size != nmemb) return 0;

    const slice = @as([*]const u8, @ptrCast(data))[0..byte_count];

    buffer.appendSlice(slice) catch return 0;

    return byte_count;
}

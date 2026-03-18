const std = @import("std");

pub const std_options = std.Options{
    .logFn = logFn,
};

fn getTime() struct { secs: u64, us: u64 } {
    var ts: std.c.timespec = undefined;
    _ = std.c.clock_gettime(std.c.CLOCK.REALTIME, &ts);
    return .{
        .secs = @intCast(ts.sec),
        .us = @intCast(@divTrunc(ts.nsec, 1000)),
    };
}

fn logFn(
    comptime level: std.log.Level,
    comptime scope: @TypeOf(.enum_literal),
    comptime fmt: []const u8,
    args: anytype,
) void {
    const level_char = comptime switch (level) {
        .debug => "D",
        .info => "I",
        .warn => "W",
        .err => "E",
    };

    const scope_str = comptime if (scope == .default) "" else "[" ++ @tagName(scope) ++ "]";

    const t = getTime();
    const epoch = std.time.epoch.EpochSeconds{ .secs = t.secs };
    const day = epoch.getEpochDay();
    const year_day = day.calculateYearDay();
    const month_day = year_day.calculateMonthDay();
    const day_secs = epoch.getDaySeconds();

    var buf: [256]u8 = undefined;
    const line = std.fmt.bufPrint(&buf, "[{d:0>4}-{d:0>2}-{d:0>2}T{d:0>2}:{d:0>2}:{d:0>2}.{d:0>6}+00:00][{s}]{s} " ++ fmt ++ "\n", .{
        year_day.year,
        month_day.month.numeric(),
        month_day.day_index + 1,
        day_secs.getHoursIntoDay(),
        day_secs.getMinutesIntoHour(),
        day_secs.getSecondsIntoMinute(),
        t.us,
        level_char,
        scope_str,
    } ++ args) catch return;

    _ = std.c.write(std.posix.STDERR_FILENO, line.ptr, line.len);
}

pub fn scoped(comptime scope: @TypeOf(.enum_literal)) type {
    return std.log.scoped(scope);
}

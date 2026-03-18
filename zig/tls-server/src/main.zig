const std = @import("std");
const c = @cImport({
    @cInclude("stdio.h");
    @cInclude("openssl/ssl.h");
    @cInclude("openssl/err.h");
    @cInclude("sys/socket.h");
    @cInclude("netinet/in.h");
    @cInclude("arpa/inet.h");
    @cInclude("unistd.h");
});

pub fn main() !void {
    // Initialize OpenSSL
    if (c.OPENSSL_init_ssl(c.OPENSSL_INIT_SSL_DEFAULT, null) != 1) {
        std.debug.print("OpenSSL init failed\n", .{});
        return error.OpenSSLInitFailed;
    }

    const ctx = c.SSL_CTX_new(c.TLS_server_method()) orelse {
        std.debug.print("SSL_CTX_new failed\n", .{});
        return error.SSLContextFailed;
    };
    defer c.SSL_CTX_free(ctx);

    if (c.SSL_CTX_use_certificate_file(ctx, "server.crt", c.SSL_FILETYPE_PEM) != 1) {
        c.ERR_print_errors_fp(c.stderr);
        return error.CertLoadFailed;
    }
    if (c.SSL_CTX_use_PrivateKey_file(ctx, "server.key", c.SSL_FILETYPE_PEM) != 1) {
        c.ERR_print_errors_fp(c.stderr);
        return error.KeyLoadFailed;
    }

    std.debug.print("TLS echo server listening on 0.0.0.0:8443\n", .{});

    // Pure C socket API (works on your exact Zig 0.16.0-dev.2905+5d71e3051)
    const listener = c.socket(c.AF_INET, c.SOCK_STREAM, 0);
    if (listener < 0) return error.SocketFailed;
    defer _ = c.close(listener);

    const opt: c_int = 1;
    if (c.setsockopt(listener, c.SOL_SOCKET, c.SO_REUSEADDR, &opt, @sizeOf(c_int)) < 0) {
        return error.SetsockoptFailed;
    }

    var addr = c.sockaddr_in{
        .sin_family = @intCast(c.AF_INET),
        .sin_port = c.htons(8443),
        .sin_addr = c.in_addr{ .s_addr = c.INADDR_ANY },
        .sin_zero = [_]u8{0} ** 8,
    };
    if (c.bind(listener, @ptrCast(&addr), @sizeOf(c.sockaddr_in)) < 0) {
        return error.BindFailed;
    }
    if (c.listen(listener, 128) < 0) {
        return error.ListenFailed;
    }

    while (true) {
        const client_fd = c.accept(listener, null, null);
        if (client_fd < 0) continue;

        // Spawn dedicated thread per connection
        const thread = std.Thread.spawn(.{}, handleConnection, .{ ctx, client_fd }) catch |err| {
            std.debug.print("Failed to spawn thread for fd {}: {}\n", .{ client_fd, err });
            _ = c.close(client_fd);
            continue;
        };
        thread.detach();
    }
}

fn handleConnection(ctx: ?*c.SSL_CTX, fd: c_int) void {
    const ssl = c.SSL_new(ctx) orelse {
        std.debug.print("SSL_new failed for fd {}\n", .{fd});
        _ = c.close(fd);
        return;
    };
    defer c.SSL_free(ssl);

    _ = c.SSL_set_fd(ssl, fd);

    if (c.SSL_accept(ssl) <= 0) {
        c.ERR_print_errors_fp(c.stderr);
        std.debug.print("TLS handshake failed for fd {}\n", .{fd});
        _ = c.close(fd);
        return;
    }

    std.debug.print("TLS connection established (fd {})\n", .{fd});

    var buf: [4096]u8 = undefined;
    while (true) {
        const bytes_read = c.SSL_read(ssl, &buf, buf.len);
        if (bytes_read <= 0) {
            const err = c.SSL_get_error(ssl, bytes_read);
            if (err != c.SSL_ERROR_ZERO_RETURN and err != c.SSL_ERROR_SYSCALL) {
                c.ERR_print_errors_fp(c.stderr);
            }
            break;
        }

        _ = c.SSL_write(ssl, &buf, bytes_read);
    }

    _ = c.SSL_shutdown(ssl);
    _ = c.close(fd);
    std.debug.print("TLS connection closed (fd {})\n", .{fd});
}

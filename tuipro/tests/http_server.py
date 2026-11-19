#!/usr/bin/env python3
"""Tiny HTTP/HTTPS fixture server for the tuipro fetch tests.

Usage:
    python3 tests/http_server.py [--port 0] [--cert cert.pem --key key.pem]

The chosen port is printed as a single line "PORT <n>" so the shell test
runner can wait for readiness and pass it to Lua.
"""
import argparse
import gzip
import json
import ssl
import sys
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def _send(self, status, body=b"", headers=None, close=True):
        self.send_response(status)
        for name, value in headers or []:
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        if close:
            self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD" and body:
            self.wfile.write(body)
        if close:
            self.close_connection = True

    def _json(self, value, status=200, headers=None):
        body = json.dumps(value).encode("utf-8")
        self._send(status, body,
                   [("Content-Type", "application/json")] + (headers or []))

    def _raw(self, payload):
        self.wfile.write(payload)
        self.wfile.flush()
        self.close_connection = True

    def _path(self):
        return self.path.split("?", 1)[0]

    def do_GET(self):
        path = self._path()
        if path in ("/", "/hello"):
            self._send(200, b"hello from the tuipro test server\n",
                       [("Content-Type", "text/plain")])
        elif path == "/json":
            self._json({"ok": True, "message": "hello", "n": 42,
                        "nested": {"a": [1, 2, 3]}})
        elif path == "/redirect":
            self._send(302, b"", [("Location", "/json")])
        elif path.startswith("/redirect-chain/"):
            n = int(path.rsplit("/", 1)[1])
            if n <= 0:
                self._json({"redirected": True})
            else:
                self._send(302, b"",
                           [("Location", "/redirect-chain/%d" % (n - 1))])
        elif path == "/redirect-absolute":
            host, port = self.server.server_address[:2]
            self._send(302, b"",
                       [("Location", "http://%s:%d/json" % (host, port))])
        elif path == "/gzip":
            body = gzip.compress(b'{"compressed":"gzip","ok":true}')
            self._send(200, body, [("Content-Type", "application/json"),
                                   ("Content-Encoding", "gzip")])
        elif path == "/deflate":
            body = zlib.compress(b'{"compressed":"deflate","ok":true}')
            self._send(200, body, [("Content-Type", "application/json"),
                                   ("Content-Encoding", "deflate")])
        elif path == "/chunked":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            for chunk in (b"hello ", b"chunked ", b"world"):
                self.wfile.write(("%x\r\n" % len(chunk)).encode() + chunk +
                                 b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
            self.close_connection = True
        elif path == "/chunked-ext":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            for chunk in (b"ext ", b"works"):
                self.wfile.write(("%x;foo=bar\r\n" % len(chunk)).encode() +
                                 chunk + b"\r\n")
            self.wfile.write(b"0;last=1\r\n\r\n")
            self.close_connection = True
        elif path == "/bad-chunk-size":
            self._raw(b"HTTP/1.1 200 OK\r\n"
                      b"Transfer-Encoding: chunked\r\n"
                      b"Connection: close\r\n\r\n"
                      b"zz\r\nhello\r\n0\r\n\r\n")
        elif path == "/bad-chunk-lf":
            self._raw(b"HTTP/1.1 200 OK\r\n"
                      b"Transfer-Encoding: chunked\r\n"
                      b"Connection: close\r\n\r\n"
                      b"5\r\nhello\rX5\r\nworld\r\n0\r\n\r\n")
        elif path == "/bad-chunk-garbage":
            self._raw(b"HTTP/1.1 200 OK\r\n"
                      b"Transfer-Encoding: chunked\r\n"
                      b"Connection: close\r\n\r\n"
                      b"5garbage\r\nhello\r\n0\r\n\r\n")
        elif path == "/bad-status":
            self._raw(b"HTTP/1.1 ABC Nope\r\nContent-Length: 0\r\n"
                      b"Connection: close\r\n\r\n")
        elif path == "/bad-content-length":
            self._raw(b"HTTP/1.1 200 OK\r\nContent-Length: 5x\r\n"
                      b"Connection: close\r\n\r\nhello")
        elif path == "/conflicting-length":
            self._raw(b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n"
                      b"Content-Length: 6\r\nConnection: close\r\n\r\nhello")
        elif path == "/truncated-length":
            self._raw(b"HTTP/1.1 200 OK\r\nContent-Length: 100\r\n"
                      b"Connection: close\r\n\r\nhello")
        elif path == "/bad-gzip":
            self._send(200, b"this is not gzip",
                       [("Content-Encoding", "gzip")])
        elif path == "/oversize-header":
            self._raw(b"HTTP/1.1 200 OK\r\nX-Big: " + b"a" * (96 * 1024) +
                      b"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
        elif path == "/method":
            self._send(200, self.command.encode())
        elif path == "/echo":
            headers = {}
            for name, value in self.headers.items():
                headers.setdefault(name.lower(), value)
            self._json({"method": self.command, "headers": headers})
        elif path == "/redirect-same":
            self._send(302, b"", [("Location", "/echo")])
        elif path == "/redirect-cross":
            port = self.server.server_address[1]
            self._send(302, b"",
                       [("Location", "http://localhost:%d/echo" % port)])
        elif path == "/redirect-post":
            self._send(303, b"", [("Location", "/method")])
        elif path == "/eof":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"body until eof")
            self.close_connection = True
        elif path == "/headers":
            self._send(200, b"headers", [
                ("X-Test", "yes"),
                ("Set-Cookie", "a=1"),
                ("Set-Cookie", "b=2"),
            ])
        elif path.startswith("/status/"):
            code = int(path.rsplit("/", 1)[1])
            self._send(code, ("status %d" % code).encode())
        elif path == "/nocontent":
            self._send(204, b"")
        elif path == "/slow":
            time.sleep(1.0)
            self._send(200, b"slow response")
        elif path == "/abort":
            time.sleep(5.0)
            self._send(200, b"too late")
        elif path == "/large":
            self._send(200, b"x" * (256 * 1024),
                       [("Content-Type", "text/plain")])
        else:
            self._send(404, b"not found")

    def do_HEAD(self):
        self.do_GET()

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(length) if length else b""
        if self._path() == "/redirect-post":
            self._send(303, b"", [("Location", "/method")])
        elif self._path() == "/echo":
            headers = {}
            for name, value in self.headers.items():
                headers.setdefault(name.lower(), value)
            self._json({
                "method": self.command,
                "path": self.path,
                "body": body.decode("utf-8", "replace"),
                "headers": headers,
            })
        else:
            self._send(405, b"method not allowed")

    do_PUT = do_POST
    do_PATCH = do_POST


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--cert")
    parser.add_argument("--key")
    args = parser.parse_args()

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    if args.cert and args.key:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(args.cert, args.key)
        server.socket = ctx.wrap_socket(server.socket, server_side=True)
    print("PORT %d" % server.server_address[1], flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()

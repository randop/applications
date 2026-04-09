#!/bin/sh

python3 -c '
import socket, ssl, time
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
with socket.create_connection(("localhost", 8484)) as sock:
    with ctx.wrap_socket(sock, server_hostname="localhost") as ssock:
        ssock.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n")
        print("Partial request sent – sleeping 40s to trigger READ timeout...")
        time.sleep(40)
        print("Sending final \\r\\n\\r\\n (too late)")
        ssock.sendall(b"\r\n\r\n")
' 2>/dev/null || echo "Server closed the connection (expected on timeout)"

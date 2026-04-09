#!/usr/bin/env bash
# test-timeouts.sh
set -euo pipefail

HOST=localhost
PORT=8484

echo "=== Testing Boost.Asio HTTPS server timeouts ==="

test_read_timeout() {
  echo "→ Testing READ timeout (30s) ..."
  python3 -c '
import socket, ssl, time
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
with socket.create_connection(("'$HOST'", '$PORT')) as s:
    with ctx.wrap_socket(s, server_hostname="'$HOST'") as ss:
        ss.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n")
        print("Partial headers sent – sleeping 40s...")
        time.sleep(40)
        ss.sendall(b"\r\n\r\n")
' 2>/dev/null || true
}

test_handshake_timeout() {
  echo "→ Testing HANDSHAKE timeout (15s) ..."
  # Force a very slow/delayed handshake by using timeout on the client side
  timeout 20s openssl s_client -connect $HOST:$PORT -quiet -no_ign_eof </dev/null 2>/dev/null || true
  echo "   (openssl client killed after 20s – server should have timed out handshake)"
}

test_write_timeout() {
  echo "→ Testing WRITE timeout (10s) ..."
  # Client accepts connection but reads extremely slowly
  python3 -c '
import socket, ssl, time
ctx = ssl.create_default_context()
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
with socket.create_connection(("'$HOST'", '$PORT')) as s:
    with ctx.wrap_socket(s, server_hostname="'$HOST'") as ss:
        ss.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
        print("Full request sent – now reading response 1 byte at a time with 2s delay...")
        for _ in range(30):
            try:
                data = ss.recv(1)
                if not data: break
                time.sleep(2)   # slow read → triggers server write timeout
            except:
                break
' 2>/dev/null || true
}

echo "Running tests (server must be running in another terminal)..."
test_read_timeout
sleep 2
test_handshake_timeout
sleep 2
test_write_timeout

echo "✅ All timeout tests completed."

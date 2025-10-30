import http.server
import socketserver
from datetime import datetime

PORT = 3000

class HelloHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/':
            self.send_response(200)
            self.send_header("Content-type", "text/plain")
            self.end_headers()
            time_string = "Current Timestamp: " + datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            self.wfile.write(time_string.encode('utf-8'))
        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Not Found")

with socketserver.TCPServer(("", PORT), HelloHandler) as httpd:
    print(f"Http API server running at http://localhost:{PORT}")
    httpd.serve_forever()

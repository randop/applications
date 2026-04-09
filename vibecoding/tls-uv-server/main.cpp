#include <openssl/err.h>
#include <openssl/ssl.h>
#include <uv.h>

#include <cstring>
#include <iostream>
#include <string>

// Per-client data
struct Client {
  uv_tcp_t tcp{};
  SSL *ssl = nullptr;
  BIO *rbio = nullptr; // Encrypted data in
  BIO *wbio = nullptr; // Encrypted data out

  std::string read_buffer; // Accumulated decrypted data
  uv_write_t write_req{};

  Client() {
    uv_tcp_init(uv_default_loop(), &tcp);
    tcp.data = this;
  }

  ~Client() {
    if (ssl) {
      SSL_free(ssl);
    }
  }
};

// Drain pending encrypted output from SSL to network
void drain_ssl_output(Client *client) {
  char outbuf[8192];
  int pending;
  while ((pending = BIO_read(client->wbio, outbuf, sizeof(outbuf))) > 0) {
    uv_buf_t wbuf = uv_buf_init(outbuf, static_cast<unsigned int>(pending));
    uv_write(&client->write_req, (uv_stream_t *)&client->tcp, &wbuf, 1,
             nullptr);
  }
}

void on_tls_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  auto *client = static_cast<Client *>(stream->data);

  if (nread <= 0) {
    uv_close((uv_handle_t *)stream,
             [](uv_handle_t *h) { delete static_cast<Client *>(h->data); });
    free(buf->base);
    return;
  }

  BIO_write(client->rbio, buf->base, static_cast<int>(nread));

  if (!SSL_is_init_finished(client->ssl)) {
    int ret = SSL_accept(client->ssl);
    if (ret <= 0) {
      int err = SSL_get_error(client->ssl, ret);
      if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
        uv_close((uv_handle_t *)stream, nullptr);
      }
    }
  } else {
    // Read decrypted application data
    char decbuf[8192];
    int bytes;
    while ((bytes = SSL_read(client->ssl, decbuf, sizeof(decbuf))) > 0) {
      client->read_buffer.append(decbuf, bytes);
    }

    if (!client->read_buffer.empty()) {
      std::cout << "=== Payload ===" << std::endl;
      std::cout << client->read_buffer << std::endl;
      std::cout << "===============" << std::endl;

      // Send minimal response
      const char *content = "<h1>Hello</h1>";

      std::string response = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/html\r\n"
                             "Content-Length: " +
                             std::to_string(std::strlen(content)) +
                             "\r\n"
                             "Connection: close\r\n"
                             "\r\n" +
                             content;

      SSL_write(client->ssl, response.data(),
                static_cast<int>(response.size()));
      drain_ssl_output(client);

      // Close after response (barebones)
      uv_close((uv_handle_t *)&client->tcp,
               [](uv_handle_t *h) { delete static_cast<Client *>(h->data); });
    }
  }

  drain_ssl_output(client);
  free(buf->base);
}

void alloc_buffer(uv_handle_t *, size_t suggested_size, uv_buf_t *buf) {
  buf->base = static_cast<char *>(malloc(suggested_size));
  buf->len = suggested_size;
}

// New connection handler
void on_new_connection(uv_stream_t *server, int status) {
  if (status < 0) {
    return;
  }

  auto *client = new Client();

  if (uv_accept(server, (uv_stream_t *)&client->tcp) == 0) {
    client->ssl = SSL_new(static_cast<SSL_CTX *>(server->data));
    client->rbio = BIO_new(BIO_s_mem());
    client->wbio = BIO_new(BIO_s_mem());
    SSL_set_bio(client->ssl, client->rbio, client->wbio);
    SSL_set_accept_state(client->ssl); // Server mode

    uv_read_start((uv_stream_t *)&client->tcp, alloc_buffer, on_tls_read);
  } else {
    delete client;
  }
}

int main() {
  uv_loop_t *loop = uv_default_loop();

  // Initialize OpenSSL
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();

  SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_server_method());
  if (!ssl_ctx) {
    std::cerr << "Failed to create SSL_CTX\n";
    return 1;
  }

  // Load certificate and private key
  if (SSL_CTX_use_certificate_file(ssl_ctx, "server.crt", SSL_FILETYPE_PEM) <=
          0 ||
      SSL_CTX_use_PrivateKey_file(ssl_ctx, "server.key", SSL_FILETYPE_PEM) <=
          0) {
    std::cerr << "Failed to load certificate/key." << std::endl;
    return 1;
  }

  if (!SSL_CTX_check_private_key(ssl_ctx)) {
    std::cerr << "Private key does not match certificate\n";
    return 1;
  }

  uv_tcp_t server{};
  uv_tcp_init(loop, &server);
  server.data = ssl_ctx;

  sockaddr_in addr{};
  uv_ip4_addr("0.0.0.0", 8443, &addr);

  uv_tcp_bind(&server, (const sockaddr *)&addr, 0);
  int r = uv_listen((uv_stream_t *)&server, SOMAXCONN, on_new_connection);
  if (r) {
    std::cerr << "Listen error: " << uv_strerror(r) << "\n";
    return 1;
  }

  std::cout << "HTTPS server listening on host 0.0.0.0 port 8443" << std::endl;

  uv_run(loop, UV_RUN_DEFAULT);

  uv_loop_close(loop);
  SSL_CTX_free(ssl_ctx);
  return 0;
}

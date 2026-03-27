#define VERSION_MAJOR 0
#define VERSION_MINOR 1
#define VERSION_PATCH 0

#define STRINGIFY0(s) #s
#define STRINGIFY(s) STRINGIFY0(s)

#define VERSION_STRING                                                         \
  STRINGIFY(VERSION_MAJOR)                                                     \
  "." STRINGIFY(VERSION_MINOR) "." STRINGIFY(VERSION_PATCH)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#define PORT 8080

static uv_loop_t *loop;

typedef struct {
  uv_tcp_t handle;
} client_t;

/* Buffer allocator for read callbacks */
static void alloc_buffer(uv_handle_t *handle, size_t suggested_size,
                         uv_buf_t *buf) {
  buf->base = (char *)malloc(suggested_size);
  buf->len = suggested_size;
}

/* Read callback: process any data, keeps connection alive, closes on
 * EOF/disconnect */
static void on_read(uv_stream_t *client_stream, ssize_t nread,
                    const uv_buf_t *buf) {
  client_t *client = (client_t *)client_stream;

  if (nread < 0) {
    if (nread != UV_EOF) {
      fprintf(stderr, "Client read error: %s\n", uv_err_name(nread));
    }
    /* client disconnected clean shutdown or error */
    uv_close((uv_handle_t *)client_stream, NULL);
  }

  free(buf->base);
}

/* client close callback cleanup */
static void on_close(uv_handle_t *handle) {
  client_t *client = (client_t *)handle;
  free(client);
  printf("Client connection closed\n");
}

/* Write callback */
static void on_write(uv_write_t *req, int status) {
  if (status < 0) {
    fprintf(stderr, "Write \"Hello\" failed: %s\n", uv_err_name(status));
  } else {
    printf("Sent \"Hello\" to client\n");
  }

  /* After successful send, start reading to keep the connection alive
   * indefinitely */
  uv_read_start((uv_stream_t *)req->handle, alloc_buffer, on_read);

  free(req);
}

/* New connection callback */
static void on_new_connection(uv_stream_t *server, int status) {
  if (status < 0) {
    fprintf(stderr, "New connection error: %s\n", uv_err_name(status));
    return;
  }

  client_t *client = (client_t *)malloc(sizeof(client_t));
  if (!client) {
    fprintf(stderr, "Out of memory for new client\n");
    return;
  }

  uv_tcp_init(loop, &client->handle);

  if (uv_accept(server, (uv_stream_t *)&client->handle) == 0) {
    printf("Client connected\n");

    /* Prepare "Hello" write */
    uv_write_t *req = (uv_write_t *)malloc(sizeof(uv_write_t));
    if (!req) {
      uv_close((uv_handle_t *)&client->handle, on_close);
      return;
    }

    uv_buf_t buf = uv_buf_init((char *)"Hello", 5);
    uv_write(req, (uv_stream_t *)&client->handle, &buf, 1, on_write);
  } else {
    uv_close((uv_handle_t *)&client->handle, on_close);
  }
}

int main(void) {
  loop = uv_default_loop();

  uv_tcp_t server;
  uv_tcp_init(loop, &server);

  struct sockaddr_in addr;
  uv_ip4_addr("0.0.0.0", PORT, &addr);

  uv_tcp_bind(&server, (const struct sockaddr *)&addr, 0);

  int r = uv_listen((uv_stream_t *)&server, 128, on_new_connection);
  if (r) {
    fprintf(stderr, "Listen error: %s\n", uv_err_name(r));
    return 1;
  }

  printf("version %s\n", VERSION_STRING);
  printf("TCP server listening on 0.0.0.0:%d\n", PORT);

  /* Run the event loop forever */
  uv_run(loop, UV_RUN_DEFAULT);

  /* Cleanup */
  uv_close((uv_handle_t *)&server, NULL);
  uv_loop_close(loop);
  return 0;
}

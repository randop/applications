#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0

#define STRINGIFY0(x) #x
#define STRINGIFY(x) STRINGIFY0(x)

#define VERSION_STRING                                                         \
  STRINGIFY(VERSION_MAJOR)                                                     \
  "." STRINGIFY(VERSION_MINOR) "." STRINGIFY(VERSION_PATCH)

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <uv.h>

#define PORT 2222
#define INTERVAL_LINE_BANNER_MS 10000
#define MAX_LINE_LENGTH 32

static unsigned long rng_state;

/* Random line generator */
static unsigned rand16(unsigned long s[1]) {
  s[0] = s[0] * 1103515245UL + 12345UL;
  return (s[0] >> 16) & 0xffff;
}

static int randline(char *line, int maxlen, unsigned long s[1]) {
  int len = 3 + rand16(s) % (maxlen - 2);
  for (int i = 0; i < len - 2; i++) {
    line[i] = 32 + rand16(s) % 95;
  }
  line[len - 2] = 13;
  line[len - 1] = 10;
  if (memcmp(line, "SSH-", 4) == 0) {
    line[0] = 'X';
  }
  return len;
}

typedef struct {
  uv_tcp_t handle;
  uv_timer_t timer;
  int refcount;
} client_t;

/* Buffer allocator for read callbacks */
static void alloc_buffer(uv_handle_t *handle, size_t suggested_size,
                         uv_buf_t *buf) {
  buf->base = (char *)malloc(suggested_size);
  buf->len = suggested_size;
}

/* Close callback: frees client resources */
static void on_close(uv_handle_t *handle) {
  client_t *client = (client_t *)handle->data;
  if (--client->refcount == 0) {
    free(client);
    printf("Client connection closed\n");
  }
}

/* Read callback: discard everything, close on disconnect/error */
static void on_read(uv_stream_t *client_stream, ssize_t nread,
                    const uv_buf_t *buf) {
  uv_handle_t *handle = (uv_handle_t *)client_stream;
  client_t *client = (client_t *)handle->data;

  if (nread < 0) {
    if (nread != UV_EOF) {
      fprintf(stderr, "Client read error: %s\n", uv_err_name(nread));
    }
    /* Close both handle and timer */
    uv_close((uv_handle_t *)&client->handle, on_close);
    uv_close((uv_handle_t *)&client->timer, on_close);
  }

  free(buf->base);
}

/* Write callback: schedule next banner line or close on error */
static void on_write(uv_write_t *req, int status) {
  client_t *client = (client_t *)((uv_handle_t *)req->handle)->data;

  if (status < 0) {
    fprintf(stderr, "Write error: %s\n", uv_err_name(status));
    uv_close((uv_handle_t *)&client->handle, on_close);
    uv_close((uv_handle_t *)&client->timer, on_close);
  } else {
    /* Schedule next random banner after DELAY */
    uv_timer_start(&client->timer, (uv_timer_cb)req->data,
                   INTERVAL_LINE_BANNER_MS, 0);
  }

  free(req);
}

/* Timer callback: send one random banner line */
static void on_timer(uv_timer_t *timer) {
  client_t *client = (client_t *)timer->data;

  char line[256];
  int len = randline(line, MAX_LINE_LENGTH, &rng_state);

  uv_write_t *req = (uv_write_t *)malloc(sizeof(uv_write_t));
  if (!req) {
    uv_close((uv_handle_t *)&client->handle, on_close);
    uv_close((uv_handle_t *)&client->timer, on_close);
    return;
  }

  /* Store timer callback pointer so on_write can restart the timer */
  req->data = (void *)on_timer;

  uv_buf_t buf = uv_buf_init(line, (unsigned int)len);
  int r = uv_write(req, (uv_stream_t *)&client->handle, &buf, 1, on_write);
  if (r < 0) {
    fprintf(stderr, "uv_write failed: %s\n", uv_err_name(r));
    free(req);
    uv_close((uv_handle_t *)&client->handle, on_close);
    uv_close((uv_handle_t *)&client->timer, on_close);
  }
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

  uv_tcp_init(uv_default_loop(), &client->handle);
  uv_timer_init(uv_default_loop(), &client->timer);

  client->handle.data = client;
  client->timer.data = client;
  client->refcount = 2;

  if (uv_accept(server, (uv_stream_t *)&client->handle) == 0) {
    printf("Client connected\n");

    /* Set tiny receive buffer + TCP_NODELAY */
    uv_os_fd_t fd;
    if (uv_fileno((uv_handle_t *)&client->handle, &fd) == 0) {
      int value = 1;
      setsockopt((int)fd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value));
      setsockopt((int)fd, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));
    }

    /* Start reading immediately (consume attacker data) */
    uv_read_start((uv_stream_t *)&client->handle, alloc_buffer, on_read);

    /* Schedule first banner line after initial DELAY */
    uv_timer_start(&client->timer, on_timer, INTERVAL_LINE_BANNER_MS, 0);
  } else {
    uv_close((uv_handle_t *)&client->handle, on_close);
    uv_close((uv_handle_t *)&client->timer, on_close);
  }
}

int main(void) {
  uv_loop_t *loop = uv_default_loop();

  /* Seed the random banner generator */
  rng_state = (unsigned long)uv_hrtime();

  uv_tcp_t server;
  uv_tcp_init(loop, &server);

  /* IPv4, IPv6 dual-stack bind */
  struct sockaddr_in6 addr;
  uv_ip6_addr("::", PORT, &addr);
  uv_tcp_bind(&server, (const struct sockaddr *)&addr, 0);

  int r = uv_listen((uv_stream_t *)&server, 128, on_new_connection);
  if (r) {
    fprintf(stderr, "Listen error: %s\n", uv_err_name(r));
    return 1;
  }

  printf("version %s\n", VERSION_STRING);
  printf("Listening on [::]:%d\n", PORT);
  printf("Listening on 0.0.0.0:%d\n", PORT);
  printf("Press Ctrl+C to stop.\n");

  uv_run(loop, UV_RUN_DEFAULT);

  /* Cleanup */
  uv_close((uv_handle_t *)&server, NULL);
  uv_loop_close(loop);
  return 0;
}

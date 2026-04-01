#define _XOPEN_SOURCE 500

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uv.h>

#define termios asmtermios
#include <asm/termios.h>
#undef termios

#include <termios.h>

static void alloc_buffer(uv_handle_t *handle, size_t suggested_size,
                         uv_buf_t *buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}

static void echo_write(uv_write_t *req, int status) {
  if (status < 0) {
    fprintf(stderr, "Write error: %s\n", uv_strerror(status));
  }
  free(req->data);
  free(req);
}

static void read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  if (nread < 0) {
    if (nread != UV_EOF) {
      fprintf(stderr, "Read error: %s\n", uv_strerror((int)nread));
    }
    uv_close((uv_handle_t *)stream, NULL);
    return;
  }

  if (nread > 0) {
    const char *msg = "Hello World\r\n";
    size_t len = strlen(msg);
    char *data = malloc(len);
    if (!data) {
      return;
    }
    memcpy(data, msg, len);

    uv_write_t *req = malloc(sizeof(uv_write_t));
    if (!req) {
      free(data);
      return;
    }
    req->data = data;
    uv_buf_t buf = uv_buf_init(data, (unsigned int)len);

    uv_write(req, stream, &buf, 1, echo_write);
  }

  if (nread > 0 && 0) {
    char *write_data = malloc((size_t)nread);
    if (write_data) {
      memcpy(write_data, buf->base, (size_t)nread);

      uv_write_t *req = malloc(sizeof(uv_write_t));

      if (req) {
        req->data = write_data;
        uv_buf_t wb = uv_buf_init(write_data, (unsigned int)nread);
        uv_write(req, stream, &wb, 1, echo_write);
      } else {
        free(write_data);
      }
    }
  }

  free(buf->base);
}

/* Set baud rate on the PTY master using Linux termios2 (supports any speed) */
static int set_baud_rate(int fd, unsigned int baud) {
  struct termios2 tio;

  if (ioctl(fd, TCGETS2, &tio) < 0) {
    perror("TCGETS2");
    return -1;
  }

  /* Clear old baud rate flags and enable custom baud rate */
  tio.c_cflag &= ~CBAUD;
  tio.c_cflag |= BOTHER; /* "B other" = custom speed */

  tio.c_ispeed = baud; /* input speed */
  tio.c_ospeed = baud; /* output speed */

  /* Common raw serial settings for a virtual USB serial device */
  tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
  tio.c_cflag |= CS8 | CREAD | CLOCAL; /* 8N1, no modem control */

  tio.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP |
                   INLCR | IGNCR | ICRNL);
  tio.c_oflag &= ~OPOST;
  tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

  if (ioctl(fd, TCSETS2, &tio) < 0) {
    perror("TCSETS2");
    return -1;
  }

  printf("Baud rate set to %u\n", baud);
  return 0;
}

int main(int argc, char **argv) {
  unsigned int baud = 115200; /* default */

  if (argc > 1) {
    baud = (unsigned int)atoi(argv[1]);
    if (baud == 0) {
      baud = 115200;
    }
  }

  /* Create PTY master */
  int master_fd = open("/dev/ptmx", O_RDWR | O_NOCTTY);
  if (master_fd == -1) {
    perror("open /dev/ptmx");
    return 1;
  }

  if (grantpt(master_fd) == -1 || unlockpt(master_fd) == -1) {
    perror("grantpt/unlockpt");
    close(master_fd);
    return 1;
  }

  char *slave_name = ptsname(master_fd);
  if (slave_name == NULL) {
    perror("ptsname");
    close(master_fd);
    return 1;
  }

  /* Apply baud rate and raw serial settings */
  if (set_baud_rate(master_fd, baud) != 0) {
    close(master_fd);
    return 1;
  }

  printf("=== Virtual USB serial echo device ready ===\n");
  printf("Device path : %s\n", slave_name);
  printf("Baud rate   : %u\n", baud);
  printf("\nTest commands:\n");
  printf("  screen %s %u\n", slave_name, baud);
  printf("  minicom -D %s -b %u\n", slave_name, baud);
  printf("  echo 'test from host' > %s\n\n", slave_name);
  printf("Press Ctrl+C to exit.\n\n");

  uv_loop_t *loop = uv_default_loop();

  uv_pipe_t pipe_handle;
  uv_pipe_init(loop, &pipe_handle, 0);
  uv_pipe_open(&pipe_handle, master_fd);

  uv_read_start((uv_stream_t *)&pipe_handle, alloc_buffer, read_cb);

  int ret = uv_run(loop, UV_RUN_DEFAULT);
  if (ret != 0) {
    fprintf(stderr, "uv_run: %s\n", uv_strerror(ret));
  }

  uv_loop_close(loop);
  return 0;
}

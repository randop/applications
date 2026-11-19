#include "tuipro_internal.h"
#include "http.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static tp_term *g_term = NULL;

double tp_now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void signal_handler(int sig)
{
  if (!g_term)
    return;
  if (sig == SIGWINCH) {
    g_term->sig_winch = 1;
  } else {
    g_term->sig_exit = 1;
    g_term->sig_number = sig;
  }
  if (g_term->sig_pipe[1] >= 0) {
    unsigned char b = (unsigned char)sig;
    ssize_t n = write(g_term->sig_pipe[1], &b, 1);
    (void)n;
  }
}

static void close_signal_pipe(tp_term *t)
{
  int r = t->sig_pipe[0];
  int w = t->sig_pipe[1];
  t->sig_pipe[0] = -1;
  t->sig_pipe[1] = -1;
  t->sig_pipe_ready = false;
  if (r >= 0)
    close(r);
  if (w >= 0)
    close(w);
}

static int open_signal_pipe(tp_term *t)
{
  t->sig_pipe[0] = -1;
  t->sig_pipe[1] = -1;
  t->sig_pipe_ready = false;
  if (pipe(t->sig_pipe) < 0)
    return -1;
  for (int i = 0; i < 2; i++) {
    int flags = fcntl(t->sig_pipe[i], F_GETFL, 0);
    if (flags >= 0)
      fcntl(t->sig_pipe[i], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(t->sig_pipe[i], F_GETFD, 0);
    if (flags >= 0)
      fcntl(t->sig_pipe[i], F_SETFD, flags | FD_CLOEXEC);
  }
  t->sig_pipe_ready = true;
  return 0;
}

static const char *signame(int sig)
{
  switch (sig) {
  case SIGINT: return "int";
  case SIGTERM: return "term";
  case SIGHUP: return "hup";
  case SIGQUIT: return "quit";
  default: return "signal";
  }
}

/* ------------------------------------------------------------------------- */
/* Output buffer                                                             */
/* ------------------------------------------------------------------------- */

void tp_out_reserve(tp_term *t, size_t extra)
{
  if (t->headless)
    return;
  size_t needed = t->out_len + extra + 1;
  if (needed <= t->out_cap)
    return;
  size_t cap = t->out_cap ? t->out_cap : 8192;
  while (cap < needed)
    cap *= 2;
  char *p = realloc(t->out, cap);
  if (!p)
    return;
  t->out = p;
  t->out_cap = cap;
}

void tp_out_append(tp_term *t, const char *s, size_t n)
{
  if (t->headless || n == 0)
    return;
  tp_out_reserve(t, n);
  if (t->out_len + n >= t->out_cap)
    return;
  memcpy(t->out + t->out_len, s, n);
  t->out_len += n;
  t->out[t->out_len] = '\0';
}

void tp_out_puts(tp_term *t, const char *s)
{
  if (s)
    tp_out_append(t, s, strlen(s));
}

void tp_out_printf(tp_term *t, const char *fmt, ...)
{
  if (t->headless)
    return;
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n <= 0) {
    va_end(ap2);
    return;
  }
  tp_out_reserve(t, (size_t)n);
  if (t->out_len + (size_t)n < t->out_cap)
    vsnprintf(t->out + t->out_len, t->out_cap - t->out_len, fmt, ap2);
  va_end(ap2);
  t->out_len += (size_t)n;
}

static void write_all(int fd, const char *data, size_t len)
{
  size_t off = 0;
  while (off < len) {
    ssize_t n = write(fd, data + off, len - off);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        struct pollfd p = { .fd = fd, .events = POLLOUT };
        poll(&p, 1, 100);
        continue;
      }
      if (g_term) {
        g_term->sig_exit = 1;
        g_term->sig_number = SIGTERM;
      }
      return;
    }
    off += (size_t)n;
  }
}

void tp_out_flush(tp_term *t)
{
  if (t->headless) {
    t->out_len = 0;
    return;
  }
  if (t->out_len > 0) {
    write_all(t->out_fd, t->out, t->out_len);
    t->out_len = 0;
  }
}

void tp_out_clear(tp_term *t)
{
  t->out_len = 0;
}

void tp_out_mode(tp_term *t, const char *mode, bool on)
{
  if (t->headless || !t->out_tty)
    return;
  tp_out_printf(t, "\x1b[?%s%c", mode, on ? 'h' : 'l');
}

/* ------------------------------------------------------------------------- */
/* Signals and size                                                          */
/* ------------------------------------------------------------------------- */

static void save_signal(int sig, struct sigaction *old)
{
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0; /* no SA_RESTART: poll must wake up */
  sigaction(sig, &sa, old);
}

int tp_term_read_size(tp_term *t, int *width, int *height)
{
  struct winsize ws;
  memset(&ws, 0, sizeof(ws));
  int w = 0, h = 0;
  if (!t->headless) {
    if (ioctl(t->out_fd, TIOCGWINSZ, &ws) == 0 || ioctl(t->in_fd, TIOCGWINSZ, &ws) == 0) {
      w = ws.ws_col;
      h = ws.ws_row;
    }
  }
  if (w <= 0) {
    const char *c = getenv("COLUMNS");
    w = c ? atoi(c) : 0;
  }
  if (h <= 0) {
    const char *l = getenv("LINES");
    h = l ? atoi(l) : 0;
  }
  if (w <= 0)
    w = 80;
  if (h <= 0)
    h = 24;
  if (w < 2)
    w = 2;
  if (h < 2)
    h = 2;
  *width = w;
  *height = h;
  return 0;
}

/* ------------------------------------------------------------------------- */
/* Color detection                                                           */
/* ------------------------------------------------------------------------- */

static bool env_truthy(const char *name)
{
  const char *v = getenv(name);
  if (!v || !*v)
    return false;
  if (strcmp(v, "0") == 0 || strcasecmp(v, "false") == 0 || strcasecmp(v, "no") == 0)
    return false;
  return true;
}

static int detect_truecolor(void)
{
  const char *force = getenv("TUIPRO_TRUECOLOR");
  if (force && *force) {
    if (strcmp(force, "0") == 0 || strcasecmp(force, "false") == 0)
      return 0;
    return 1;
  }
  const char *ct = getenv("COLORTERM");
  if (ct && (strstr(ct, "truecolor") || strstr(ct, "24bit")))
    return 1;
  const char *term = getenv("TERM");
  if (term && strstr(term, "direct"))
    return 1;
  const char *prog = getenv("TERM_PROGRAM");
  if (prog) {
    static const char *const names[] = {
      "iTerm.app", "WezTerm", "vscode", "ghostty", "kitty",
      "Alacritty", "Hyper", "Tabby", "rio", "WarpTerminal", NULL
    };
    for (int i = 0; names[i]; i++) {
      if (strcmp(prog, names[i]) == 0)
        return 1;
    }
  }
  return 0;
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

tp_term *tp_term_new(void)
{
  tp_term *t = calloc(1, sizeof(*t));
  if (!t)
    return NULL;
  t->in_fd = STDIN_FILENO;
  t->out_fd = STDOUT_FILENO;
  t->out_x = -1;
  t->out_y = -1;
  t->cur_fg = TP_DEFAULT_COLOR;
  t->cur_bg = TP_DEFAULT_COLOR;
  t->cur_cursor_x = -1;
  t->cur_cursor_y = -1;
  t->cur_cursor_visible = true;
  t->cur_cursor_shape = TP_CURSOR_BLOCK;
  return t;
}

void tp_event_release(tp_event *ev)
{
  if (!ev)
    return;
  free(ev->data);
  ev->data = NULL;
  ev->data_len = 0;
}

void tp_input_free(tp_term *t)
{
  free(t->paste_buf);
  t->paste_buf = NULL;
  t->paste_len = 0;
  t->paste_cap = 0;
  t->in_len = 0;
  t->paste_mode = false;
  t->seq_pending = false;
}

void tp_input_reset(tp_term *t)
{
  tp_input_free(t);
}

void tp_grid_destroy(tp_term *t)
{
  free(t->grid.front);
  free(t->grid.back);
  t->grid.front = NULL;
  t->grid.back = NULL;
  t->grid.width = 0;
  t->grid.height = 0;
}

void tp_term_free(tp_term *t)
{
  if (!t)
    return;
  tp_term_shutdown(t);
  tp_grid_destroy(t);
  tp_input_free(t);
  free(t->out);
  if (g_term == t)
    g_term = NULL;
  free(t);
}

const char *tp_term_error(const tp_term *t)
{
  return t ? t->error : "no terminal";
}

bool tp_term_active(const tp_term *t)
{
  return t && t->active;
}

int tp_term_width(const tp_term *t)
{
  return t ? t->width : 0;
}

int tp_term_height(const tp_term *t)
{
  return t ? t->height : 0;
}

int tp_term_out_fd(const tp_term *t)
{
  return t ? t->out_fd : -1;
}

bool tp_term_truecolor(const tp_term *t)
{
  return t && t->truecolor;
}

void tp_term_request_exit(tp_term *t)
{
  if (t) {
    t->sig_exit = 1;
    t->sig_number = 0;
  }
}

bool tp_term_exit_requested(const tp_term *t)
{
  return t && t->sig_exit;
}

static int make_raw(tp_term *t)
{
  if (tcgetattr(t->in_fd, &t->orig_termios) < 0) {
    snprintf(t->error, sizeof(t->error), "tcgetattr: %s", strerror(errno));
    return -1;
  }
  t->termios_saved = true;
  struct termios raw = t->orig_termios;
  cfmakeraw(&raw);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(t->in_fd, TCSAFLUSH, &raw) < 0) {
    snprintf(t->error, sizeof(t->error), "tcsetattr: %s", strerror(errno));
    return -1;
  }
  return 0;
}

void tp_term_restore(tp_term *t)
{
  if (!t || t->headless)
    return;
  if (t->mouse_enabled)
    tp_term_set_mouse(t, false);
  if (t->paste_enabled)
    tp_term_set_paste(t, false);
  if (t->wrap_disabled)
    tp_out_mode(t, "7", true);
  tp_out_puts(t, "\x1b[0m");
  tp_out_puts(t, "\x1b[0 q");
  tp_out_puts(t, "\x1b[?25h");
  tp_out_puts(t, "\x1b[?1049l");
  tp_out_flush(t);
  if (t->termios_saved) {
    tcsetattr(t->in_fd, TCSAFLUSH, &t->orig_termios);
    t->termios_saved = false;
  }
}

void tp_term_shutdown(tp_term *t)
{
  if (!t || !t->active)
    return;
  if (!t->headless) {
    tp_term_restore(t);
    if (t->signals_saved) {
      sigaction(SIGWINCH, &t->old_winch, NULL);
      sigaction(SIGINT, &t->old_int, NULL);
      sigaction(SIGTERM, &t->old_term, NULL);
      sigaction(SIGHUP, &t->old_hup, NULL);
      sigaction(SIGQUIT, &t->old_quit, NULL);
      sigaction(SIGPIPE, &t->old_pipe, NULL);
      t->signals_saved = false;
    }
  }
  close_signal_pipe(t);
  t->active = false;
  t->out_len = 0;
  t->out_x = -1;
  t->out_y = -1;
  if (g_term == t)
    g_term = NULL;
}

int tp_term_enter(tp_term *t)
{
  if (!t->out_tty)
    return 0;
  tp_out_puts(t, "\x1b[?1049h");
  tp_out_puts(t, "\x1b[?25l");
  tp_out_mode(t, "7", false);
  t->wrap_disabled = true;
  if (t->paste_enabled) {
    tp_out_mode(t, "2004", true);
    t->paste_enabled = true;
  }
  if (t->mouse_enabled)
    tp_out_puts(t, "\x1b[?1000h\x1b[?1002h\x1b[?1006h");
  tp_out_puts(t, "\x1b[2J\x1b[H");
  t->out_x = 0;
  t->out_y = 0;
  t->sgr_valid = false;
  t->cursor_dirty = true;
  return 0;
}

int tp_term_init(tp_term *t, bool headless, int width, int height, int truecolor)
{
  if (!t)
    return -1;
  if (t->active)
    tp_term_shutdown(t);
  tp_grid_destroy(t);
  tp_input_free(t);
  t->out_len = 0;
  t->error[0] = '\0';
  t->sig_winch = 0;
  t->sig_exit = 0;
  t->sig_number = 0;
  t->eof = false;
  t->sig_pipe[0] = -1;
  t->sig_pipe[1] = -1;
  t->sig_pipe_ready = false;
  t->headless = headless;
  t->in_fd = STDIN_FILENO;
  t->out_fd = STDOUT_FILENO;
  t->in_tty = !headless && isatty(STDIN_FILENO);
  t->out_tty = !headless && isatty(STDOUT_FILENO);
  t->truecolor = truecolor < 0 ? detect_truecolor() : truecolor != 0;
  t->sync_output = env_truthy("TUIPRO_SYNC") || getenv("TUIPRO_SYNC") == NULL;
  t->mouse_enabled = false;
  t->paste_enabled = true;
  t->wrap_disabled = false;
  t->cursor_initialized = false;
  t->sgr_valid = false;
  t->out_x = -1;
  t->out_y = -1;
  t->clip_x = 0;
  t->clip_y = 0;
  t->clip_w = 0;
  t->clip_h = 0;
  t->clip_enabled = false;

  if (headless) {
    t->width = width > 0 ? width : 80;
    t->height = height > 0 ? height : 24;
    tp_grid_init(t, t->width, t->height);
    t->active = true;
    t->cur_cursor_visible = false;
    return 0;
  }

  if (!t->in_tty || !t->out_tty) {
    snprintf(t->error, sizeof(t->error),
             "tuipro needs a terminal on stdin/stdout (try --headless for scripts)");
    return -1;
  }

  int w, h;
  tp_term_read_size(t, &w, &h);
  t->width = width > 0 ? width : w;
  t->height = height > 0 ? height : h;

  if (make_raw(t) < 0)
    return -1;

  open_signal_pipe(t);
  g_term = t;
  save_signal(SIGWINCH, &t->old_winch);
  save_signal(SIGINT, &t->old_int);
  save_signal(SIGTERM, &t->old_term);
  save_signal(SIGHUP, &t->old_hup);
  save_signal(SIGQUIT, &t->old_quit);
  {
    struct sigaction ign;
    memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGPIPE, &ign, &t->old_pipe);
  }
  t->signals_saved = true;

  tp_grid_init(t, t->width, t->height);
  t->active = true;
  tp_term_enter(t);
  tp_term_set_paste(t, true);
  tp_out_flush(t);
  return 0;
}

/* ------------------------------------------------------------------------- */
/* Cursor and terminal features                                              */
/* ------------------------------------------------------------------------- */

void tp_term_set_cursor(tp_term *t, int x, int y, bool visible, tp_cursor_shape shape)
{
  if (!t)
    return;
  t->cur_cursor_x = x;
  t->cur_cursor_y = y;
  t->cur_cursor_visible = visible;
  t->cur_cursor_shape = shape;
  t->cursor_dirty = true;
}

void tp_term_set_title(tp_term *t, const char *title)
{
  if (!t || t->headless || !t->out_tty)
    return;
  char clean[512];
  size_t j = 0;
  for (size_t i = 0; title && title[i] && j + 1 < sizeof(clean); i++) {
    unsigned char c = (unsigned char)title[i];
    if (c < 32 || c == 127)
      continue;
    clean[j++] = (char)c;
  }
  clean[j] = '\0';
  tp_out_printf(t, "\x1b]0;%s\x1b\\", clean);
  tp_out_flush(t);
}

void tp_term_bell(tp_term *t)
{
  if (!t || t->headless || !t->out_tty)
    return;
  tp_out_puts(t, "\a");
  tp_out_flush(t);
}

static char *base64_encode(const unsigned char *data, size_t len)
{
  static const char b64[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t out_len = ((len + 2) / 3) * 4;
  char *out = malloc(out_len + 1);
  if (!out)
    return NULL;
  size_t i, j = 0;
  for (i = 0; i + 2 < len; i += 3) {
    unsigned int n = (unsigned int)data[i] << 16 |
                     (unsigned int)data[i + 1] << 8 |
                     (unsigned int)data[i + 2];
    out[j++] = b64[(n >> 18) & 63];
    out[j++] = b64[(n >> 12) & 63];
    out[j++] = b64[(n >> 6) & 63];
    out[j++] = b64[n & 63];
  }
  if (i < len) {
    unsigned int n = (unsigned int)data[i] << 16;
    out[j++] = b64[(n >> 18) & 63];
    if (i + 1 < len) {
      n |= (unsigned int)data[i + 1] << 8;
      out[j++] = b64[(n >> 12) & 63];
      out[j++] = b64[(n >> 6) & 63];
      out[j++] = '=';
    } else {
      out[j++] = b64[(n >> 12) & 63];
      out[j++] = '=';
      out[j++] = '=';
    }
  }
  out[j] = '\0';
  return out;
}

void tp_term_copy(tp_term *t, const char *text)
{
  if (!t || t->headless || !t->out_tty || !text)
    return;
  char *enc = base64_encode((const unsigned char *)text, strlen(text));
  if (!enc)
    return;
  tp_out_printf(t, "\x1b]52;c;%s\x1b\\", enc);
  tp_out_flush(t);
  free(enc);
}

void tp_term_set_mouse(tp_term *t, bool enabled)
{
  if (!t)
    return;
  t->mouse_enabled = enabled;
  if (t->headless || !t->active)
    return;
  if (enabled) {
    tp_out_puts(t, "\x1b[?1000h\x1b[?1002h\x1b[?1006h");
  } else {
    tp_out_puts(t, "\x1b[?1006l\x1b[?1002l\x1b[?1000l");
  }
  tp_out_flush(t);
}

void tp_term_set_paste(tp_term *t, bool enabled)
{
  if (!t)
    return;
  t->paste_enabled = enabled;
  if (t->headless || !t->active)
    return;
  tp_out_mode(t, "2004", enabled);
  tp_out_flush(t);
}

/* ------------------------------------------------------------------------- */
/* Event polling                                                             */
/* ------------------------------------------------------------------------- */

static int make_resize_event(tp_term *t, tp_event *ev)
{
  int w, h;
  tp_term_read_size(t, &w, &h);
  if (w == t->width && h == t->height)
    return 0;
  tp_grid_resize(t, w, h);
  memset(ev, 0, sizeof(*ev));
  ev->type = TP_EVENT_RESIZE;
  ev->width = w;
  ev->height = h;
  t->sig_winch = 0;
  return 1;
}

static int make_signal_event(tp_term *t, tp_event *ev)
{
  memset(ev, 0, sizeof(*ev));
  ev->type = TP_EVENT_SIGNAL;
  if (t->sig_number) {
    snprintf(ev->signal, sizeof(ev->signal), "%s", signame((int)t->sig_number));
  } else if (t->eof) {
    snprintf(ev->signal, sizeof(ev->signal), "eof");
  } else {
    snprintf(ev->signal, sizeof(ev->signal), "exit");
  }
  return 1;
}

int tp_term_poll(tp_term *t, int timeout_ms, tp_event *ev)
{
  if (!t || !ev)
    return -1;
  memset(ev, 0, sizeof(*ev));
  if (!t->active)
    return -1;
  if (t->headless) {
    /* Scripts and headless apps still need the network loop to make progress. */
    tp_http_wait(-1, -1, timeout_ms);
    return 0;
  }

  double start = tp_now_ms();
  bool infinite = timeout_ms < 0;

  for (;;) {
    if (t->sig_exit || t->eof) {
      if (t->sig_winch && make_resize_event(t, ev))
        return 1;
      return make_signal_event(t, ev);
    }
    if (t->sig_winch && make_resize_event(t, ev))
      return 1;

    if (tp_input_parse(t, ev))
      return 1;

    double now = tp_now_ms();
    int wait_ms;
    if (infinite) {
      wait_ms = -1;
    } else {
      double remaining = (double)timeout_ms - (now - start);
      if (remaining <= 0)
        return 0;
      wait_ms = (int)remaining;
      if (wait_ms < 0)
        wait_ms = 0;
    }
    if (t->seq_pending) {
      double rem = t->seq_deadline - tp_now_ms();
      if (rem < 0)
        rem = 0;
      if (wait_ms < 0 || (int)rem < wait_ms)
        wait_ms = (int)rem;
    }

    int flags = tp_http_wait(t->in_fd,
                             t->sig_pipe_ready ? t->sig_pipe[0] : -1,
                             wait_ms);
    if (flags & 1) {
      unsigned char buf[4096];
      ssize_t n = read(t->in_fd, buf, sizeof(buf));
      if (n > 0) {
        tp_input_feed(t, buf, (size_t)n);
      } else if (n == 0) {
        t->eof = true;
      } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
        t->eof = true;
      }
    }
    if (flags & 2) {
      /* The self-pipe was drained by the libuv callback.  Re-check the
       * sig_winch/sig_exit flags at the top of the loop. */
    }
  }
}

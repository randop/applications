#ifndef TUIPRO_INTERNAL_H
#define TUIPRO_INTERNAL_H

#include "tuipro.h"

#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <termios.h>

#define TP_MAX_COMBINING 2
#define TP_INBUFSZ 16384
#define TP_ESC_TIMEOUT_MS 30.0

typedef struct tp_grid {
  int width, height;
  tp_cell *front; /* what the terminal currently shows */
  tp_cell *back;  /* what widgets have drawn for the next frame */
  bool full_redraw;
} tp_grid;

struct tp_term {
  int in_fd;
  int out_fd;
  bool headless;
  bool active;
  bool in_tty;
  bool out_tty;
  bool truecolor;
  bool mouse_enabled;
  bool paste_enabled;
  bool sync_output;
  bool wrap_disabled;
  bool cursor_initialized;
  int width;
  int height;

  struct termios orig_termios;
  bool termios_saved;
  struct sigaction old_winch;
  struct sigaction old_int;
  struct sigaction old_term;
  struct sigaction old_hup;
  struct sigaction old_quit;
  struct sigaction old_pipe;
  bool signals_saved;

  tp_grid grid;

  /* clipping */
  int clip_x, clip_y, clip_w, clip_h;
  bool clip_enabled;

  /* output buffering */
  char *out;
  size_t out_len;
  size_t out_cap;

  /* terminal state cache */
  int out_x, out_y;       /* cursor position as far as we know; -1 unknown */
  uint32_t cur_fg, cur_bg;
  uint16_t cur_attrs;
  bool sgr_valid;
  int cur_cursor_x, cur_cursor_y;
  bool cur_cursor_visible;
  tp_cursor_shape cur_cursor_shape;
  bool cursor_dirty;

  /* input */
  unsigned char inbuf[TP_INBUFSZ];
  size_t in_len;
  bool paste_mode;
  char *paste_buf;
  size_t paste_len;
  size_t paste_cap;
  bool seq_pending;
  double seq_deadline;

  /* signals */
  volatile sig_atomic_t sig_winch;
  volatile sig_atomic_t sig_exit;
  volatile sig_atomic_t sig_number;
  bool eof;

  char error[512];
};

/* output helpers (term.c) */
void tp_out_reserve(tp_term *t, size_t extra);
void tp_out_append(tp_term *t, const char *s, size_t n);
void tp_out_puts(tp_term *t, const char *s);
void tp_out_printf(tp_term *t, const char *fmt, ...);
void tp_out_flush(tp_term *t);
void tp_out_clear(tp_term *t);
void tp_out_mode(tp_term *t, const char *mode, bool on);

/* grid helpers (grid.c) */
void tp_grid_init(tp_term *t, int width, int height);
void tp_grid_destroy(tp_term *t);
void tp_grid_resize(tp_term *t, int width, int height);
char *tp_grid_row_alloc(tp_term *t, int row, bool trim);
uint32_t tp_utf8_decode(const char *s, size_t len, size_t *consumed);
int tp_utf8_encode(uint32_t cp, char out[4]);

/* input helpers (input.c) */
void tp_input_reset(tp_term *t);
void tp_input_feed(tp_term *t, const void *data, size_t len);
int tp_input_parse(tp_term *t, tp_event *ev); /* 1 event, 0 incomplete */
void tp_input_free(tp_term *t);

/* internal terminal setup helpers (term.c) */
void tp_term_restore(tp_term *t);
int tp_term_enter(tp_term *t);

#endif /* TUIPRO_INTERNAL_H */

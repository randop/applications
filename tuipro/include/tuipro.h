/*
 * tuipro - a terminal user interface container with a Lua widget toolkit.
 *
 * The C layer owns the terminal, a double-buffered cell grid, keyboard and
 * mouse parsing and the event loop primitives.  The Lua layer (see lua/)
 * builds containers and widgets on top of this small API.
 */
#ifndef TUIPRO_H
#define TUIPRO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TUIPRO_VERSION_STRING "0.1.0"
#define TP_DEFAULT_COLOR UINT32_MAX

typedef struct tp_term tp_term;

typedef enum {
  TP_ATTR_BOLD = 1 << 0,
  TP_ATTR_DIM = 1 << 1,
  TP_ATTR_ITALIC = 1 << 2,
  TP_ATTR_UNDERLINE = 1 << 3,
  TP_ATTR_BLINK = 1 << 4,
  TP_ATTR_REVERSE = 1 << 5,
  TP_ATTR_STRIKE = 1 << 6,
  TP_ATTR_OVERLINE = 1 << 7,
} tp_attr;

typedef enum {
  TP_EVENT_NONE = 0,
  TP_EVENT_KEY,
  TP_EVENT_MOUSE,
  TP_EVENT_RESIZE,
  TP_EVENT_PASTE,
  TP_EVENT_SIGNAL,
  TP_EVENT_FOCUS,
} tp_event_type;

typedef enum {
  TP_MOD_SHIFT = 1 << 0,
  TP_MOD_ALT = 1 << 1,
  TP_MOD_CTRL = 1 << 2,
  TP_MOD_SUPER = 1 << 3,
} tp_keymod;

typedef enum {
  TP_MOUSE_PRESS = 0,
  TP_MOUSE_RELEASE,
  TP_MOUSE_MOVE,
  TP_MOUSE_WHEEL,
} tp_mouse_action;

typedef enum {
  TP_CURSOR_DEFAULT = 0,
  TP_CURSOR_BLOCK,
  TP_CURSOR_UNDERLINE,
  TP_CURSOR_BAR,
} tp_cursor_shape;

typedef struct tp_event {
  tp_event_type type;
  char key[32];      /* "a", "up", "f1", "enter", "backspace", ... */
  char text[64];     /* printable UTF-8 text, if any */
  int mods;          /* TP_MOD_* */
  char button[16];   /* "left", "wheel-up", ... */
  tp_mouse_action action;
  int x, y;          /* 0-based cell coordinates */
  int width, height; /* resize */
  char *data;        /* paste payload (owned; call tp_event_release) */
  size_t data_len;
  char signal[16];
} tp_event;

typedef struct tp_cell {
  uint32_t codepoint;      /* 0 marks a wide-cell continuation */
  uint32_t combining[2];
  uint32_t fg, bg;         /* 0xRRGGBB or TP_DEFAULT_COLOR */
  uint16_t attrs;
  uint8_t width;           /* 0 (continuation), 1 or 2 */
  uint8_t _reserved;
} tp_cell;

/* Terminal lifecycle ------------------------------------------------------ */
tp_term *tp_term_new(void);
void tp_term_free(tp_term *t);
/* Returns 0 on success, -1 on failure (see tp_term_error). */
int tp_term_init(tp_term *t, bool headless, int width, int height,
                 int truecolor /* -1 auto, 0 off, 1 on */);
void tp_term_shutdown(tp_term *t);
const char *tp_term_error(const tp_term *t);
bool tp_term_active(const tp_term *t);
int tp_term_width(const tp_term *t);
int tp_term_height(const tp_term *t);
int tp_term_out_fd(const tp_term *t);
bool tp_term_truecolor(const tp_term *t);

/* Events ------------------------------------------------------------------ */
/* timeout_ms < 0 blocks forever.  Returns 1 when an event was produced,
 * 0 on timeout, -1 on EOF/error. */
int tp_term_poll(tp_term *t, int timeout_ms, tp_event *ev);
void tp_event_release(tp_event *ev);
void tp_term_request_exit(tp_term *t);
bool tp_term_exit_requested(const tp_term *t);
double tp_now_ms(void);

/* Drawing ----------------------------------------------------------------- */
void tp_grid_clear(tp_term *t);
/* Draw UTF-8 text at 0-based (x,y).  Returns the x coordinate after the last
 * drawn cell.  A wide character occupies its base cell plus a continuation. */
int tp_grid_put(tp_term *t, int x, int y, const char *utf8,
                uint32_t fg, uint32_t bg, uint16_t attrs);
void tp_grid_fill(tp_term *t, int x, int y, int w, int h, const char *utf8,
                  uint32_t fg, uint32_t bg, uint16_t attrs);
void tp_grid_set_clip(tp_term *t, int x, int y, int w, int h);
void tp_grid_reset_clip(tp_term *t);
void tp_grid_flush(tp_term *t);
void tp_term_set_cursor(tp_term *t, int x, int y, bool visible,
                        tp_cursor_shape shape);
int tp_text_width(const char *utf8);

/* Snapshots (useful for headless use and tests) --------------------------- */
char *tp_grid_row_utf8(tp_term *t, int row); /* malloc'd; caller frees */
int tp_grid_row_width(tp_term *t);           /* terminal width */
bool tp_grid_cell_at(tp_term *t, int x, int y, tp_cell *out);

/* Misc terminal features -------------------------------------------------- */
void tp_term_set_title(tp_term *t, const char *title);
void tp_term_bell(tp_term *t);
void tp_term_copy(tp_term *t, const char *text);
void tp_term_set_mouse(tp_term *t, bool enabled);
void tp_term_set_paste(tp_term *t, bool enabled);
int tp_term_read_size(tp_term *t, int *width, int *height);

#ifdef __cplusplus
}
#endif

#endif /* TUIPRO_H */

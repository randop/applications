#include "tuipro_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ------------------------------------------------------------------------- */
/* UTF-8 helpers                                                             */
/* ------------------------------------------------------------------------- */

uint32_t tp_utf8_decode(const char *s, size_t len, size_t *consumed)
{
  const unsigned char *p = (const unsigned char *)s;
  if (!len) {
    *consumed = 0;
    return 0;
  }
  unsigned char c = p[0];
  if (c < 0x80) {
    *consumed = 1;
    return c;
  }
  int n;
  uint32_t cp;
  if ((c & 0xE0) == 0xC0) {
    n = 2;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    n = 3;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    n = 4;
    cp = c & 0x07;
  } else {
    *consumed = 1;
    return 0xFFFD;
  }
  if ((size_t)n > len) {
    *consumed = 1;
    return 0xFFFD;
  }
  for (int i = 1; i < n; i++) {
    if ((p[i] & 0xC0) != 0x80) {
      *consumed = 1;
      return 0xFFFD;
    }
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) ||
      (n == 4 && cp < 0x10000) || cp > 0x10FFFF ||
      (cp >= 0xD800 && cp <= 0xDFFF)) {
    *consumed = 1;
    return 0xFFFD;
  }
  *consumed = (size_t)n;
  return cp;
}

int tp_utf8_encode(uint32_t cp, char out[4])
{
  if (cp <= 0x7F) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp <= 0x7FF) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp <= 0xFFFF) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

static bool is_wide_fallback(uint32_t cp)
{
  return (cp >= 0x1100 && cp <= 0x115F) ||
         (cp >= 0x2E80 && cp <= 0x303E) ||
         (cp >= 0x3041 && cp <= 0x33FF) ||
         (cp >= 0x3400 && cp <= 0x4DBF) ||
         (cp >= 0x4E00 && cp <= 0x9FFF) ||
         (cp >= 0xA000 && cp <= 0xA4CF) ||
         (cp >= 0xAC00 && cp <= 0xD7A3) ||
         (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFE10 && cp <= 0xFE19) ||
         (cp >= 0xFE30 && cp <= 0xFE6F) ||
         (cp >= 0xFF00 && cp <= 0xFF60) ||
         (cp >= 0xFFE0 && cp <= 0xFFE6) ||
         (cp >= 0x1F300 && cp <= 0x1FAFF) ||
         (cp >= 0x20000 && cp <= 0x3FFFD);
}

static int char_width(uint32_t cp)
{
  if (cp == 0)
    return 1;
  if (cp < 32 || (cp >= 0x7F && cp < 0xA0))
    return 1; /* controls are rendered as spaces */
  int w = wcwidth((wchar_t)cp);
  if (w == 0)
    return 0;
  if (w < 0)
    return is_wide_fallback(cp) ? 2 : 1;
  return w > 2 ? 2 : w;
}

int tp_text_width(const char *utf8)
{
  if (!utf8)
    return 0;
  int width = 0;
  const char *p = utf8;
  size_t len = strlen(utf8);
  while (len) {
    size_t used = 0;
    uint32_t cp = tp_utf8_decode(p, len, &used);
    if (!used)
      break;
    width += char_width(cp);
    p += used;
    len -= used;
  }
  return width;
}

/* ------------------------------------------------------------------------- */
/* Grid allocation                                                           */
/* ------------------------------------------------------------------------- */

static void blank_cell(tp_cell *c)
{
  memset(c, 0, sizeof(*c));
  c->codepoint = ' ';
  c->fg = TP_DEFAULT_COLOR;
  c->bg = TP_DEFAULT_COLOR;
  c->width = 1;
}

void tp_grid_init(tp_term *t, int width, int height)
{
  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;
  tp_grid *g = &t->grid;
  g->width = width;
  g->height = height;
  size_t count = (size_t)width * (size_t)height;
  g->back = malloc(count * sizeof(tp_cell));
  g->front = malloc(count * sizeof(tp_cell));
  if (!g->back || !g->front) {
    free(g->back);
    free(g->front);
    g->back = g->front = NULL;
    g->width = g->height = 0;
    return;
  }
  for (size_t i = 0; i < count; i++)
    blank_cell(&g->back[i]);
  memset(g->front, 0xFF, count * sizeof(tp_cell));
  g->full_redraw = true;
  t->width = width;
  t->height = height;
  t->clip_x = 0;
  t->clip_y = 0;
  t->clip_w = width;
  t->clip_h = height;
  t->clip_enabled = false;
  t->out_x = -1;
  t->out_y = -1;
  t->sgr_valid = false;
}

void tp_grid_resize(tp_term *t, int width, int height)
{
  if (t->grid.back) {
    free(t->grid.front);
    free(t->grid.back);
    t->grid.front = NULL;
    t->grid.back = NULL;
  }
  tp_grid_init(t, width, height);
  t->cur_cursor_x = -1;
  t->cur_cursor_y = -1;
  t->out_x = -1;
  t->out_y = -1;
  if (!t->headless && t->active) {
    tp_out_puts(t, "\x1b[2J\x1b[H");
    tp_out_flush(t);
  }
}

void tp_grid_clear(tp_term *t)
{
  if (!t || !t->grid.back)
    return;
  size_t count = (size_t)t->grid.width * (size_t)t->grid.height;
  for (size_t i = 0; i < count; i++)
    blank_cell(&t->grid.back[i]);
}

/* ------------------------------------------------------------------------- */
/* Clipping                                                                  */
/* ------------------------------------------------------------------------- */

void tp_grid_set_clip(tp_term *t, int x, int y, int w, int h)
{
  if (!t)
    return;
  int x2 = x + w, y2 = y + h;
  int cx1 = x < 0 ? 0 : x;
  int cy1 = y < 0 ? 0 : y;
  int cx2 = x2 > t->width ? t->width : x2;
  int cy2 = y2 > t->height ? t->height : y2;
  if (cx2 < cx1)
    cx2 = cx1;
  if (cy2 < cy1)
    cy2 = cy1;
  t->clip_x = cx1;
  t->clip_y = cy1;
  t->clip_w = cx2 - cx1;
  t->clip_h = cy2 - cy1;
  t->clip_enabled = true;
}

void tp_grid_reset_clip(tp_term *t)
{
  if (!t)
    return;
  t->clip_x = 0;
  t->clip_y = 0;
  t->clip_w = t->width;
  t->clip_h = t->height;
  t->clip_enabled = false;
}

static bool cell_visible(tp_term *t, int x, int y, int w)
{
  if (y < 0 || y >= t->height || x < 0 || x + w > t->width)
    return false;
  if (!t->clip_enabled)
    return true;
  return x >= t->clip_x && x + w <= t->clip_x + t->clip_w &&
         y >= t->clip_y && y < t->clip_y + t->clip_h;
}

/* ------------------------------------------------------------------------- */
/* Cell mutation                                                             */
/* ------------------------------------------------------------------------- */

static void raw_blank(tp_grid *g, int x, int y)
{
  blank_cell(&g->back[(size_t)y * (size_t)g->width + (size_t)x]);
}

static void sanitize(tp_grid *g, int x, int y)
{
  if (x < 0 || x >= g->width || y < 0 || y >= g->height)
    return;
  tp_cell *c = &g->back[(size_t)y * (size_t)g->width + (size_t)x];
  if (c->width == 0) {
    /* This is the right half of a wide glyph: remove the left half too. */
    if (x > 0)
      raw_blank(g, x - 1, y);
    raw_blank(g, x, y);
  } else if (c->width == 2) {
    raw_blank(g, x, y);
    if (x + 1 < g->width)
      raw_blank(g, x + 1, y);
  } else {
    raw_blank(g, x, y);
  }
}

static void put_cell(tp_grid *g, int x, int y, uint32_t cp, int width,
                     uint32_t fg, uint32_t bg, uint16_t attrs)
{
  sanitize(g, x, y);
  tp_cell *c = &g->back[(size_t)y * (size_t)g->width + (size_t)x];
  c->codepoint = cp;
  c->fg = fg;
  c->bg = bg;
  c->attrs = attrs;
  c->width = (uint8_t)width;
  c->combining[0] = 0;
  c->combining[1] = 0;
  if (width == 2) {
    sanitize(g, x + 1, y);
    tp_cell *n = &g->back[(size_t)y * (size_t)g->width + (size_t)x + 1];
    n->codepoint = 0;
    n->fg = fg;
    n->bg = bg;
    n->attrs = attrs;
    n->width = 0;
    n->combining[0] = 0;
    n->combining[1] = 0;
  }
}

static void add_combining(tp_grid *g, int x, int y, uint32_t cp)
{
  if (x < 0 || x >= g->width || y < 0 || y >= g->height)
    return;
  tp_cell *c = &g->back[(size_t)y * (size_t)g->width + (size_t)x];
  if (c->width == 0 && x > 0)
    c = &g->back[(size_t)y * (size_t)g->width + (size_t)(x - 1)];
  if (c->width == 0)
    return;
  for (int i = 0; i < TP_MAX_COMBINING; i++) {
    if (c->combining[i] == 0) {
      c->combining[i] = cp;
      return;
    }
  }
}

/* ------------------------------------------------------------------------- */
/* Public drawing                                                            */
/* ------------------------------------------------------------------------- */

int tp_grid_put(tp_term *t, int x, int y, const char *utf8,
                uint32_t fg, uint32_t bg, uint16_t attrs)
{
  if (!t || !t->grid.back || !utf8)
    return x;
  tp_grid *g = &t->grid;
  int height = t->clip_enabled ? t->clip_h : g->height;
  int top = t->clip_enabled ? t->clip_y : 0;
  int clip_right = t->clip_enabled ? t->clip_x + t->clip_w : g->width;
  if (y < top || y >= top + height)
    return x;
  int cx = x;
  const char *p = utf8;
  size_t len = strlen(utf8);
  while (len) {
    size_t used = 0;
    uint32_t cp = tp_utf8_decode(p, len, &used);
    if (!used)
      break;
    p += used;
    len -= used;
    if (cp == '\t') {
      int next = (cx + 8) & ~7;
      while (cx < next && cx < clip_right) {
        if (cell_visible(t, cx, y, 1))
          put_cell(g, cx, y, ' ', 1, fg, bg, attrs);
        cx++;
      }
      continue;
    }
    if (cp < 32 || (cp >= 0x7F && cp < 0xA0))
      cp = ' ';
    int width = char_width(cp);
    if (width == 0) {
      add_combining(g, cx - 1, y, cp);
      continue;
    }
    if (width > 2)
      width = 2;
    if (cx + width > g->width)
      break;
    if (cx >= clip_right)
      break;
    if (cell_visible(t, cx, y, width))
      put_cell(g, cx, y, cp, width, fg, bg, attrs);
    cx += width;
  }
  return cx;
}

void tp_grid_fill(tp_term *t, int x, int y, int w, int h, const char *utf8,
                  uint32_t fg, uint32_t bg, uint16_t attrs)
{
  if (!t || !t->grid.back || w <= 0 || h <= 0)
    return;
  if (!utf8 || !*utf8)
    utf8 = " ";
  int x2 = x + w;
  int y2 = y + h;
  if (x2 > t->width)
    x2 = t->width;
  if (y2 > t->height)
    y2 = t->height;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  for (int row = y; row < y2; row++) {
    int col = x;
    int guard = 0;
    while (col < x2 && guard++ < t->width * 2) {
      int next = tp_grid_put(t, col, row, utf8, fg, bg, attrs);
      if (next <= col)
        break;
      col = next;
    }
  }
}

/* ------------------------------------------------------------------------- */
/* Snapshot                                                                  */
/* ------------------------------------------------------------------------- */

static char *row_utf8(tp_term *t, int row, bool trim)
{
  if (!t || !t->grid.back || row < 0 || row >= t->grid.height)
    return NULL;
  size_t cap = (size_t)t->grid.width * 8 + 1;
  char *buf = malloc(cap);
  if (!buf)
    return NULL;
  size_t len = 0;
  tp_cell *cells = &t->grid.back[(size_t)row * (size_t)t->grid.width];
  int trim_to = t->grid.width;
  if (trim) {
    while (trim_to > 0) {
      tp_cell *c = &cells[trim_to - 1];
      if (c->codepoint != ' ' || c->width == 0)
        break;
      trim_to--;
    }
  }
  for (int x = 0; x < trim_to; x++) {
    tp_cell *c = &cells[x];
    if (c->width == 0)
      continue;
    uint32_t cp = c->codepoint ? c->codepoint : ' ';
    char enc[4];
    int n = tp_utf8_encode(cp, enc);
    if (len + (size_t)n + 12 >= cap) {
      cap *= 2;
      char *nb = realloc(buf, cap);
      if (!nb) {
        free(buf);
        return NULL;
      }
      buf = nb;
    }
    memcpy(buf + len, enc, (size_t)n);
    len += (size_t)n;
    for (int i = 0; i < TP_MAX_COMBINING; i++) {
      if (!c->combining[i])
        break;
      n = tp_utf8_encode(c->combining[i], enc);
      if (len + (size_t)n + 12 >= cap) {
        cap *= 2;
        char *nb = realloc(buf, cap);
        if (!nb) {
          free(buf);
          return NULL;
        }
        buf = nb;
      }
      memcpy(buf + len, enc, (size_t)n);
      len += (size_t)n;
    }
    if (c->width == 2) {
      /* Account for the skipped continuation; keep snapshot width aligned. */
    }
  }
  buf[len] = '\0';
  return buf;
}

char *tp_grid_row_utf8(tp_term *t, int row)
{
  return row_utf8(t, row, false);
}

char *tp_grid_row_alloc(tp_term *t, int row, bool trim)
{
  return row_utf8(t, row, trim);
}

int tp_grid_row_width(tp_term *t)
{
  return t ? t->grid.width : 0;
}

bool tp_grid_cell_at(tp_term *t, int x, int y, tp_cell *out)
{
  if (!t || !out || !t->grid.back)
    return false;
  if (x < 0 || x >= t->grid.width || y < 0 || y >= t->grid.height)
    return false;
  *out = t->grid.back[(size_t)y * (size_t)t->grid.width + (size_t)x];
  return true;
}

/* ------------------------------------------------------------------------- */
/* Diff rendering                                                            */
/* ------------------------------------------------------------------------- */

static bool cells_equal(const tp_cell *a, const tp_cell *b)
{
  return a->codepoint == b->codepoint &&
         a->fg == b->fg && a->bg == b->bg &&
         a->attrs == b->attrs && a->width == b->width &&
         a->combining[0] == b->combining[0] &&
         a->combining[1] == b->combining[1];
}

static int quantize_component(int v)
{
  if (v < 48)
    return 0;
  if (v < 115)
    return 1;
  return (v - 35) / 40;
}

static int rgb_to_256(uint32_t color)
{
  int r = (int)((color >> 16) & 0xFF);
  int g = (int)((color >> 8) & 0xFF);
  int b = (int)(color & 0xFF);
  if (r == g && g == b) {
    if (r < 8)
      return 16;
    if (r > 248)
      return 231;
    return 232 + (r - 8) * 24 / 247;
  }
  int ri = quantize_component(r);
  int gi = quantize_component(g);
  int bi = quantize_component(b);
  return 16 + 36 * ri + 6 * gi + bi;
}

static void append_color(tp_term *t, bool fg, uint32_t color)
{
  if (color == TP_DEFAULT_COLOR) {
    tp_out_puts(t, fg ? ";39" : ";49");
  } else if (t->truecolor) {
    tp_out_printf(t, ";%d;2;%d;%d;%d", fg ? 38 : 48,
                  (int)((color >> 16) & 0xFF), (int)((color >> 8) & 0xFF),
                  (int)(color & 0xFF));
  } else {
    tp_out_printf(t, ";%d;5;%d", fg ? 38 : 48, rgb_to_256(color));
  }
}

static void emit_sgr(tp_term *t, uint32_t fg, uint32_t bg, uint16_t attrs)
{
  if (t->sgr_valid && t->cur_fg == fg && t->cur_bg == bg && t->cur_attrs == attrs)
    return;
  tp_out_puts(t, "\x1b[0");
  if (attrs & TP_ATTR_BOLD) tp_out_puts(t, ";1");
  if (attrs & TP_ATTR_DIM) tp_out_puts(t, ";2");
  if (attrs & TP_ATTR_ITALIC) tp_out_puts(t, ";3");
  if (attrs & TP_ATTR_UNDERLINE) tp_out_puts(t, ";4");
  if (attrs & TP_ATTR_BLINK) tp_out_puts(t, ";5");
  if (attrs & TP_ATTR_REVERSE) tp_out_puts(t, ";7");
  if (attrs & TP_ATTR_STRIKE) tp_out_puts(t, ";9");
  if (attrs & TP_ATTR_OVERLINE) tp_out_puts(t, ";53");
  append_color(t, true, fg);
  append_color(t, false, bg);
  tp_out_puts(t, "m");
  t->sgr_valid = true;
  t->cur_fg = fg;
  t->cur_bg = bg;
  t->cur_attrs = attrs;
}

static void move_cursor(tp_term *t, int x, int y)
{
  if (t->out_x == x && t->out_y == y)
    return;
  tp_out_printf(t, "\x1b[%d;%dH", y + 1, x + 1);
  t->out_x = x;
  t->out_y = y;
}

static void emit_cell(tp_term *t, const tp_cell *c)
{
  char enc[4];
  uint32_t cp = c->codepoint ? c->codepoint : ' ';
  int n = tp_utf8_encode(cp, enc);
  tp_out_append(t, enc, (size_t)n);
  for (int i = 0; i < TP_MAX_COMBINING; i++) {
    if (!c->combining[i])
      break;
    n = tp_utf8_encode(c->combining[i], enc);
    tp_out_append(t, enc, (size_t)n);
  }
}

static void apply_cursor(tp_term *t)
{
  if (t->headless || !t->out_tty)
    return;
  bool visible = t->cur_cursor_visible;
  int cx = t->cur_cursor_x;
  int cy = t->cur_cursor_y;
  if (cx < 0 || cx >= t->width || cy < 0 || cy >= t->height)
    visible = false;

  if (t->cursor_dirty) {
    int shape;
    switch (t->cur_cursor_shape) {
    case TP_CURSOR_UNDERLINE: shape = 4; break;
    case TP_CURSOR_BAR: shape = 6; break;
    case TP_CURSOR_BLOCK: shape = 2; break;
    default: shape = 0; break;
    }
    tp_out_printf(t, "\x1b[%d q", shape);
    tp_out_puts(t, visible ? "\x1b[?25h" : "\x1b[?25l");
    t->cursor_initialized = true;
    t->cursor_dirty = false;
  } else if (visible && !t->cursor_initialized) {
    tp_out_puts(t, "\x1b[?25h");
    t->cursor_initialized = true;
  }

  if (visible)
    move_cursor(t, cx, cy);
}

void tp_grid_flush(tp_term *t)
{
  if (!t || !t->grid.back || !t->grid.front)
    return;
  tp_grid *g = &t->grid;
  int w = g->width;
  int h = g->height;

  if (t->headless) {
    memcpy(g->front, g->back, (size_t)w * (size_t)h * sizeof(tp_cell));
    g->full_redraw = false;
    return;
  }

  if (!t->out_tty)
    return;

  if (t->sync_output)
    tp_out_puts(t, "\x1b[?2026h");

  if (g->full_redraw) {
    tp_out_puts(t, "\x1b[2J");
    t->out_x = -1;
    t->out_y = -1;
    t->sgr_valid = false;
  }

  for (int y = 0; y < h; y++) {
    tp_cell *front = &g->front[(size_t)y * (size_t)w];
    tp_cell *back = &g->back[(size_t)y * (size_t)w];
    int x = 0;
    while (x < w) {
      while (x < w && !g->full_redraw && cells_equal(&front[x], &back[x]))
        x++;
      if (x >= w)
        break;
      int start = x;
      int last = x;
      while (x < w) {
        if (g->full_redraw || !cells_equal(&front[x], &back[x]))
          last = x;
        if (x - last > 8)
          break;
        x++;
      }
      int end = last + 1;
      /* Expand to full wide glyphs. */
      while (start > 0 && back[start].width == 0)
        start--;
      while (end < w && back[end].width == 0)
        end++;
      if (end > w)
        end = w;

      move_cursor(t, start, y);
      for (int col = start; col < end; col++) {
        tp_cell *c = &back[col];
        if (c->width == 0)
          continue;
        emit_sgr(t, c->fg, c->bg, c->attrs);
        emit_cell(t, c);
        col += c->width - 1;
        t->out_x = col + 1;
        t->out_y = y;
      }
      x = end;
    }
  }

  memcpy(g->front, g->back, (size_t)w * (size_t)h * sizeof(tp_cell));
  g->full_redraw = false;

  apply_cursor(t);

  if (t->sync_output)
    tp_out_puts(t, "\x1b[?2026l");
  tp_out_flush(t);
}

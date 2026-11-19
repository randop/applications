#include "tuipro_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Buffer helpers                                                            */
/* ------------------------------------------------------------------------- */

void tp_input_feed(tp_term *t, const void *data, size_t len)
{
  if (!t || !data || !len)
    return;
  size_t room = TP_INBUFSZ - t->in_len;
  if (len > room) {
    if (room == 0)
      return;
    len = room;
  }
  memcpy(t->inbuf + t->in_len, data, len);
  t->in_len += len;
}

static void consume(tp_term *t, size_t n)
{
  if (n >= t->in_len) {
    t->in_len = 0;
    return;
  }
  memmove(t->inbuf, t->inbuf + n, t->in_len - n);
  t->in_len -= n;
}

static void set_key(tp_event *ev, const char *key, int mods, const char *text)
{
  ev->type = TP_EVENT_KEY;
  snprintf(ev->key, sizeof(ev->key), "%s", key ? key : "");
  ev->mods = mods;
  if (text)
    snprintf(ev->text, sizeof(ev->text), "%s", text);
}

/* Returns 1 when an event was produced. */
static int pending(tp_term *t, tp_event *ev)
{
  double now = tp_now_ms();
  if (t->seq_pending && now >= t->seq_deadline) {
    t->seq_pending = false;
    if (t->in_len == 1 && t->inbuf[0] == 0x1b) {
      consume(t, 1);
      set_key(ev, "esc", 0, NULL);
      return 1;
    }
    /* A broken escape sequence: drop it rather than printing garbage. */
    t->in_len = 0;
    return 0;
  }
  if (!t->seq_pending) {
    t->seq_pending = true;
    t->seq_deadline = now + TP_ESC_TIMEOUT_MS;
  }
  return 0;
}

static void paste_append(tp_term *t, const char *data, size_t len)
{
  if (t->paste_len + len + 1 > t->paste_cap) {
    size_t cap = t->paste_cap ? t->paste_cap : 256;
    while (cap < t->paste_len + len + 1)
      cap *= 2;
    char *p = realloc(t->paste_buf, cap);
    if (!p)
      return;
    t->paste_buf = p;
    t->paste_cap = cap;
  }
  memcpy(t->paste_buf + t->paste_len, data, len);
  t->paste_len += len;
  t->paste_buf[t->paste_len] = '\0';
}

/* ------------------------------------------------------------------------- */
/* Mouse                                                                     */
/* ------------------------------------------------------------------------- */

static const char *button_name(int code)
{
  switch (code & 3) {
  case 0: return "left";
  case 1: return "middle";
  case 2: return "right";
  default: return "none";
  }
}

static int parse_sgr_mouse(tp_term *t, const char *body, size_t len, int final,
                           tp_event *ev)
{
  /* body looks like "<0;12;5" */
  if (len < 1 || body[0] != '<')
    return 0;
  int vals[3] = { 0, 0, 0 };
  int vi = 0;
  int cur = 0;
  bool any = false;
  for (size_t i = 1; i <= len; i++) {
    char c = i < len ? body[i] : ';';
    if (c >= '0' && c <= '9') {
      cur = cur * 10 + (c - '0');
      any = true;
    } else if (c == ';' || c == ':') {
      if (vi < 3)
        vals[vi++] = cur;
      cur = 0;
      any = false;
    } else {
      return 0;
    }
  }
  if (vi == 2 && any)
    vals[vi++] = cur;
  if (vi < 2)
    return 0;

  int b = vals[0];
  memset(ev, 0, sizeof(*ev));
  ev->type = TP_EVENT_MOUSE;
  ev->x = vals[1] > 0 ? vals[1] - 1 : 0;
  ev->y = vals[2] > 0 ? vals[2] - 1 : 0;
  if (b & 4)
    ev->mods |= TP_MOD_SHIFT;
  if (b & 8)
    ev->mods |= TP_MOD_ALT;
  if (b & 16)
    ev->mods |= TP_MOD_CTRL;

  if (b & 64) {
    ev->action = TP_MOUSE_WHEEL;
    switch (b & 3) {
    case 0: snprintf(ev->button, sizeof(ev->button), "wheel-up"); break;
    case 1: snprintf(ev->button, sizeof(ev->button), "wheel-down"); break;
    case 2: snprintf(ev->button, sizeof(ev->button), "wheel-left"); break;
    default: snprintf(ev->button, sizeof(ev->button), "wheel-right"); break;
    }
  } else if (b & 32) {
    ev->action = TP_MOUSE_MOVE;
    snprintf(ev->button, sizeof(ev->button), "%s", button_name(b));
  } else if (final == 'm' || (b & 3) == 3) {
    ev->action = TP_MOUSE_RELEASE;
    snprintf(ev->button, sizeof(ev->button), "%s", button_name(b));
  } else {
    ev->action = TP_MOUSE_PRESS;
    snprintf(ev->button, sizeof(ev->button), "%s", button_name(b));
  }
  consume(t, len + 3); /* ESC [ < body final */
  return 1;
}

/* ------------------------------------------------------------------------- */
/* Key sequence tables                                                       */
/* ------------------------------------------------------------------------- */

static int parse_mods(int param)
{
  if (param <= 1)
    return 0;
  int bits = param - 1;
  int mods = 0;
  if (bits & 1)
    mods |= TP_MOD_SHIFT;
  if (bits & 2)
    mods |= TP_MOD_ALT;
  if (bits & 4)
    mods |= TP_MOD_CTRL;
  if (bits & 8)
    mods |= TP_MOD_SUPER;
  return mods;
}

static const char *kitty_key(uint32_t cp)
{
  if (cp >= 57344 && cp <= 57398) {
    static const char *const special[] = {
      "esc", "enter", "tab", "backspace", "insert", "delete", "left", "right",
      "up", "down", "pageup", "pagedown", "home", "end", "caps-lock",
      "scroll-lock", "num-lock", "print-screen", "pause", "menu",
      "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10",
      "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18", "f19",
      "f20", "f21", "f22", "f23", "f24", "f25", "f26", "f27", "f28",
      "f29", "f30", "f31", "f32", "f33", "f34", "f35",
    };
    size_t idx = (size_t)(cp - 57344);
    if (idx < sizeof(special) / sizeof(special[0]))
      return special[idx];
  }
  if (cp >= 57417 && cp <= 57427) {
    static const char *const kp[] = {
      "k-left", "k-right", "k-up", "k-down", "k-pageup", "k-pagedown",
      "k-home", "k-end", "k-insert", "k-delete", "k-begin",
    };
    size_t idx = (size_t)(cp - 57417);
    if (idx < sizeof(kp) / sizeof(kp[0]))
      return kp[idx];
  }
  return NULL;
}

static void key_from_codepoint(tp_event *ev, uint32_t cp, int mods)
{
  char text[8] = { 0 };
  if (cp == 13) {
    set_key(ev, "enter", mods, NULL);
    return;
  }
  if (cp == 27) {
    set_key(ev, "esc", mods, NULL);
    return;
  }
  if (cp == 9) {
    set_key(ev, "tab", mods, NULL);
    return;
  }
  if (cp == 127 || cp == 8) {
    set_key(ev, "backspace", mods, NULL);
    return;
  }
  if (cp == 32) {
    set_key(ev, "space", mods, " ");
    return;
  }
  const char *name = kitty_key(cp);
  if (name) {
    set_key(ev, name, mods, NULL);
    return;
  }
  if (cp < 32 || cp > 0x10FFFF) {
    set_key(ev, "unknown", mods, NULL);
    return;
  }
  int n = tp_utf8_encode(cp, text);
  text[n] = '\0';
  set_key(ev, text, mods, text);
}

/* Parse a CSI sequence.  Returns 1 if an event was produced, 0 if incomplete,
 * -1 if the sequence should be silently ignored. */
static int parse_csi(tp_term *t, tp_event *ev)
{
  size_t len = t->in_len;
  if (len < 3)
    return 0;
  size_t final_idx = 0;
  bool found = false;
  for (size_t i = 2; i < len; i++) {
    unsigned char c = t->inbuf[i];
    if (c >= 0x40 && c <= 0x7E) {
      final_idx = i;
      found = true;
      break;
    }
  }
  if (!found)
    return 0;
  int final = t->inbuf[final_idx];
  const char *body = (const char *)t->inbuf + 2;
  size_t body_len = final_idx - 2;

  /* Bracketed paste. */
  if (final == '~' && body_len == 3) {
    if (memcmp(body, "200", 3) == 0) {
      t->paste_mode = true;
      t->paste_len = 0;
      consume(t, final_idx + 1);
      return -1;
    }
    if (memcmp(body, "201", 3) == 0) {
      t->paste_mode = false;
      consume(t, final_idx + 1);
      return -1;
    }
  }

  /* SGR mouse. */
  if ((final == 'M' || final == 'm') && body_len > 0 && body[0] == '<') {
    return parse_sgr_mouse(t, body, body_len, final, ev) ? 1 : 0;
  }

  /* Focus in/out. */
  if (body_len == 0 && (final == 'I' || final == 'O')) {
    memset(ev, 0, sizeof(*ev));
    ev->type = TP_EVENT_FOCUS;
    ev->action = final == 'I' ? (tp_mouse_action)1 : (tp_mouse_action)0;
    consume(t, final_idx + 1);
    return 1;
  }

  /* Parameter list. */
  int params[8] = { 0 };
  int nparams = 0;
  int cur = 0;
  bool have_digit = false;
  for (size_t i = 0; i < body_len; i++) {
    char c = body[i];
    if (c >= '0' && c <= '9') {
      cur = cur * 10 + (c - '0');
      have_digit = true;
    } else if (c == ';') {
      if (nparams < 8)
        params[nparams++] = cur;
      cur = 0;
      have_digit = false;
    } else if (c == ':') {
      /* sub-parameter, ignore the remainder of this parameter */
      while (i + 1 < body_len && body[i + 1] != ';')
        i++;
    } else {
      /* private prefix or unknown: handled above; ignore sequence */
      consume(t, final_idx + 1);
      return -1;
    }
  }
  if (have_digit && nparams < 8)
    params[nparams++] = cur;

  int mods = nparams > 1 ? parse_mods(params[1]) : 0;

  /* CSI u (kitty keyboard protocol / fixterms). */
  if (final == 'u') {
    if (nparams < 1) {
      consume(t, final_idx + 1);
      return -1;
    }
    key_from_codepoint(ev, (uint32_t)params[0], mods);
    consume(t, final_idx + 1);
    return 1;
  }

  /* modifyOtherKeys: CSI 27;mod;code~ */
  if (final == '~' && nparams >= 3 && params[0] == 27) {
    int m = parse_mods(params[1]);
    key_from_codepoint(ev, (uint32_t)params[2], m);
    consume(t, final_idx + 1);
    return 1;
  }

  const char *key = NULL;
  int extra_mods = 0;
  switch (final) {
  case 'A': key = "up"; break;
  case 'B': key = "down"; break;
  case 'C': key = "right"; break;
  case 'D': key = "left"; break;
  case 'E': key = "begin"; break;
  case 'F': key = "end"; break;
  case 'H': key = "home"; break;
  case 'P': key = "f1"; break;
  case 'Q': key = "f2"; break;
  case 'R': key = "f3"; break;
  case 'S': key = "f4"; break;
  case 'Z': key = "tab"; extra_mods = TP_MOD_SHIFT; break;
  case '~':
    if (nparams < 1) {
      consume(t, final_idx + 1);
      return -1;
    }
    switch (params[0]) {
    case 1: case 7: key = "home"; break;
    case 2: key = "insert"; break;
    case 3: key = "delete"; break;
    case 4: case 8: key = "end"; break;
    case 5: key = "pageup"; break;
    case 6: key = "pagedown"; break;
    case 11: key = "f1"; break;
    case 12: key = "f2"; break;
    case 13: key = "f3"; break;
    case 14: key = "f4"; break;
    case 15: key = "f5"; break;
    case 17: key = "f6"; break;
    case 18: key = "f7"; break;
    case 19: key = "f8"; break;
    case 20: key = "f9"; break;
    case 21: key = "f10"; break;
    case 23: key = "f11"; break;
    case 24: key = "f12"; break;
    case 25: key = "f13"; break;
    case 26: key = "f14"; break;
    case 28: key = "f15"; break;
    case 29: key = "f16"; break;
    case 31: key = "f17"; break;
    case 32: key = "f18"; break;
    case 33: key = "f19"; break;
    case 34: key = "f20"; break;
    default: break;
    }
    break;
  default:
    break;
  }

  consume(t, final_idx + 1);
  if (!key)
    return -1;
  memset(ev, 0, sizeof(*ev));
  set_key(ev, key, mods | extra_mods, NULL);
  return 1;
}

static int parse_ss3(tp_term *t, tp_event *ev)
{
  if (t->in_len < 3)
    return 0;
  int c = t->inbuf[2];
  const char *key = NULL;
  switch (c) {
  case 'A': key = "up"; break;
  case 'B': key = "down"; break;
  case 'C': key = "right"; break;
  case 'D': key = "left"; break;
  case 'H': key = "home"; break;
  case 'F': key = "end"; break;
  case 'P': key = "f1"; break;
  case 'Q': key = "f2"; break;
  case 'R': key = "f3"; break;
  case 'S': key = "f4"; break;
  case 'M': key = "enter"; break;
  default: break;
  }
  if (!key)
    return 0;
  consume(t, 3);
  memset(ev, 0, sizeof(*ev));
  set_key(ev, key, 0, NULL);
  return 1;
}

/* ------------------------------------------------------------------------- */
/* Main parser                                                               */
/* ------------------------------------------------------------------------- */

int tp_input_parse(tp_term *t, tp_event *ev)
{
  if (!t || !t->in_len)
    return 0;
  memset(ev, 0, sizeof(*ev));

  for (;;) {
    if (t->in_len == 0)
      return 0;

    /* Bracketed paste collects everything up to the end marker. */
    if (t->paste_mode) {
      static const char endmark[] = "\x1b[201~";
      const size_t mlen = sizeof(endmark) - 1;
      unsigned char *found = memmem(t->inbuf, t->in_len, endmark, mlen);
      if (found) {
        size_t before = (size_t)(found - t->inbuf);
        paste_append(t, (const char *)t->inbuf, before);
        consume(t, before + mlen);
        t->paste_mode = false;
        t->seq_pending = false;
        ev->type = TP_EVENT_PASTE;
        ev->data = malloc(t->paste_len + 1);
        if (ev->data) {
          memcpy(ev->data, t->paste_buf, t->paste_len);
          ev->data[t->paste_len] = '\0';
          ev->data_len = t->paste_len;
        }
        t->paste_len = 0;
        return 1;
      }
      size_t safe = t->in_len > mlen - 1 ? t->in_len - (mlen - 1) : 0;
      if (safe == 0)
        return 0;
      paste_append(t, (const char *)t->inbuf, safe);
      consume(t, safe);
      continue;
    }

    unsigned char c = t->inbuf[0];
    if (c != 0x1b) {
      if (c == 0x0d || c == 0x0a) {
        consume(t, 1);
        set_key(ev, "enter", 0, NULL);
        return 1;
      }
      if (c == 0x09) {
        consume(t, 1);
        set_key(ev, "tab", 0, NULL);
        return 1;
      }
      if (c == 0x7f) {
        consume(t, 1);
        set_key(ev, "backspace", 0, NULL);
        return 1;
      }
      if (c == 0x08) {
        consume(t, 1);
        set_key(ev, "h", TP_MOD_CTRL, NULL);
        return 1;
      }
      if (c == 0x00) {
        consume(t, 1);
        set_key(ev, "space", TP_MOD_CTRL, " ");
        return 1;
      }
      if (c < 0x20) {
        consume(t, 1);
        char key[8] = { 0 };
        int mods = TP_MOD_CTRL;
        if (c >= 0x01 && c <= 0x1a) {
          key[0] = (char)('a' + c - 1);
        } else {
          switch (c) {
          case 0x1c: key[0] = '\\'; break;
          case 0x1d: key[0] = ']'; break;
          case 0x1e: key[0] = '^'; break;
          case 0x1f: key[0] = '_'; break;
          default: snprintf(key, sizeof(key), "control"); break;
          }
        }
        set_key(ev, key, mods, NULL);
        return 1;
      }
      if (c < 0x80) {
        consume(t, 1);
        char text[2] = { (char)c, 0 };
        if (c == ' ') {
          set_key(ev, "space", 0, " ");
        } else {
          set_key(ev, text, 0, text);
        }
        return 1;
      }
      /* UTF-8 character. */
      int need;
      if ((c & 0xE0) == 0xC0)
        need = 2;
      else if ((c & 0xF0) == 0xE0)
        need = 3;
      else if ((c & 0xF8) == 0xF0)
        need = 4;
      else {
        consume(t, 1);
        set_key(ev, "unknown", 0, "\xEF\xBF\xBD");
        return 1;
      }
      if (t->in_len < (size_t)need)
        return pending(t, ev);
      size_t used = 0;
      uint32_t cp = tp_utf8_decode((const char *)t->inbuf, (size_t)need, &used);
      if (used != (size_t)need || cp == 0xFFFD) {
        consume(t, 1);
        set_key(ev, "unknown", 0, "\xEF\xBF\xBD");
        return 1;
      }
      char text[8] = { 0 };
      memcpy(text, t->inbuf, (size_t)need);
      consume(t, (size_t)need);
      set_key(ev, text, 0, text);
      return 1;
    }

    /* ESC ... */
    if (t->in_len == 1)
      return pending(t, ev);

    unsigned char next = t->inbuf[1];
    if (next == 0x1b) {
      consume(t, 1);
      set_key(ev, "esc", 0, NULL);
      return 1;
    }
    if (next == '[') {
      int rc = parse_csi(t, ev);
      if (rc < 0) {
        t->seq_pending = false;
        continue;
      }
      if (rc == 0)
        return pending(t, ev);
      t->seq_pending = false;
      return 1;
    }
    if (next == 'O') {
      int rc = parse_ss3(t, ev);
      if (rc == 0)
        return pending(t, ev);
      t->seq_pending = false;
      return 1;
    }

    /* Alt + key. */
    unsigned char alt = t->inbuf[1];
    if (alt < 0x20) {
      char key[16];
      int mods = TP_MOD_ALT;
      if (alt == 0x0d || alt == 0x0a) {
        consume(t, 2);
        set_key(ev, "enter", mods, NULL);
        return 1;
      }
      if (alt == 0x09) {
        consume(t, 2);
        set_key(ev, "tab", mods, NULL);
        return 1;
      }
      if (alt == 0x7f) {
        consume(t, 2);
        set_key(ev, "backspace", mods, NULL);
        return 1;
      }
      if (alt >= 1 && alt <= 26) {
        key[0] = (char)('a' + alt - 1);
        key[1] = '\0';
        mods |= TP_MOD_CTRL;
      } else {
        snprintf(key, sizeof(key), "control");
      }
      consume(t, 2);
      set_key(ev, key, mods, NULL);
      return 1;
    }
    if (alt < 0x80) {
      char text[2] = { (char)alt, 0 };
      consume(t, 2);
      if (alt == ' ')
        set_key(ev, "space", TP_MOD_ALT, " ");
      else
        set_key(ev, text, TP_MOD_ALT, text);
      return 1;
    }

    /* Alt + UTF-8. */
    int need;
    if ((alt & 0xE0) == 0xC0)
      need = 2;
    else if ((alt & 0xF0) == 0xE0)
      need = 3;
    else if ((alt & 0xF8) == 0xF0)
      need = 4;
    else {
      consume(t, 2);
      set_key(ev, "unknown", TP_MOD_ALT, NULL);
      return 1;
    }
    if (t->in_len < (size_t)need + 1)
      return pending(t, ev);
    size_t used = 0;
    uint32_t cp = tp_utf8_decode((const char *)t->inbuf + 1, (size_t)need, &used);
    if (used != (size_t)need || cp == 0xFFFD) {
      consume(t, 2);
      set_key(ev, "unknown", TP_MOD_ALT, NULL);
      return 1;
    }
    char text[8] = { 0 };
    memcpy(text, t->inbuf + 1, (size_t)need);
    consume(t, (size_t)need + 1);
    set_key(ev, text, TP_MOD_ALT, text);
    return 1;
  }
}

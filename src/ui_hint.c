/* ui_hint.c: see ui_hint.h. */
#include "ui_hint.h"
#include "ck.h"
#include "input.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* the web shell's .pb: A green, B red, X blue, Y yellow */
static const struct { const char *k; uint32_t bg, fg; } FACE[] = {
  {"A", 0x3aa655, 0xffffff}, {"B", 0xd6453a, 0xffffff}, {"X", 0x3a7bd6, 0xffffff}, {"Y", 0xe0b62a, C_INK}};

float hint_h(float fs) { return roundf(fs * 1.5f); }
static float glyph_fs(float fs) { return roundf(fs * 0.95f); }
static float gap(float fs) { return roundf(fs * 0.4f); }

static int face(const char *t) {
  for (int i = 0; i < 4; i++) if (!strcmp(FACE[i].k, t)) return i;
  return -1;
}
/* a token's width: kind '{' pad, '[' key, '<' soft */
static float glyph_w(float fs, char kind, const char *t) {
  float h = hint_h(fs), gf = glyph_fs(fs);
  if (kind == '{' && face(t) >= 0) return h;
  float tw = t_width(F_LCD, gf, 0, t);
  float w = tw + roundf(fs * (kind == '{' ? 0.9f : 0.7f));
  return fmaxf(w, h);
}
static void glyph_paint(cv *c, float fs, char kind, const char *t, float x, float cy) {
  float h = hint_h(fs), gf = glyph_fs(fs), w = glyph_w(fs, kind, t), top = roundf(cy - h / 2);
  float tw = t_width(F_LCD, gf, 0, t), base = roundf(cy + gf * 0.36f);
  int f = kind == '{' ? face(t) : -1;
  if (f >= 0) {
    cv_begin(c); cv_arc(c, x + h / 2, top + h / 2, h / 2, 0, 2 * CK_PI, false); cv_close(c);
    cv_fill_hex(c, C_INK, 1); cv_fill(c);
    cv_begin(c); cv_arc(c, x + h / 2, top + h / 2, h / 2 - 2, 0, 2 * CK_PI, false); cv_close(c);
    cv_fill_hex(c, FACE[f].bg, 1); cv_fill(c);
    t_draw(c, F_LCD, gf, 0, x + (h - tw) / 2, base, t, COL(FACE[f].fg));
    return;
  }
  if (kind == '{') {   /* a pill: the shoulders, the triggers, START */
    cv_begin(c); cv_round_rect(c, x, top, w, h, h / 2); cv_close(c); cv_fill_hex(c, C_INK, 1); cv_fill(c);
    cv_begin(c); cv_round_rect(c, x + 2, top + 2, w - 4, h - 4, h / 2 - 2); cv_close(c); cv_fill_hex(c, 0x3b352d, 1); cv_fill(c);
    t_draw(c, F_LCD, gf, 0, x + (w - tw) / 2, base, t, COL(C_CREAM));
    return;
  }
  if (kind == '[') {   /* a key: ink, cream letters, a cream edge so it reads over the dark scene too */
    c_rect(c, x, top, w, h, COL(C_CREAM));
    c_rect(c, x + 2, top + 2, w - 4, h - 4, COL(C_INK));
    t_draw(c, F_LCD, gf, 0, x + (w - tw) / 2, base, t, COL(C_CREAM));
    return;
  }
  c_box(c, x, top, w, h, COL(C_PAPER), 2, COL(C_INK), 0, 0, COL(0));   /* '<': the mouse, a phone pad */
  t_draw(c, F_LCD, gf, 0, x + (w - tw) / 2, base, t, COL(C_INK));
}

/* walks s: text runs and glyphs; paints when c is set, returns the width */
static float walk(cv *c, const hint_style *st, float x, float cy, const char *s) {
  float x0 = x;
  char run[200]; int n = 0;
  float base = roundf(cy + st->fs * 0.36f);
  for (const char *p = s;; p++) {
    char close = *p == '{' ? '}' : *p == '[' ? ']' : *p == '<' ? '>' : 0;
    const char *end = close ? strchr(p + 1, close) : NULL;
    if (!*p || end) {
      if (n) {
        run[n] = 0;
        if (c) t_draw_sh(c, F_TYPE, st->fs, st->ls, x, base, run, st->text, st->sh);
        x += t_width(F_TYPE, st->fs, st->ls, run);
        n = 0;
      }
      if (!*p) break;
      char tok[24]; int k = (int)(end - p - 1); if (k > 23) k = 23;
      memcpy(tok, p + 1, k); tok[k] = 0;
      if (c) glyph_paint(c, st->fs, *p, tok, x, cy);
      x += glyph_w(st->fs, *p, tok) + gap(st->fs);
      p = end;
    } else if (n < 199) run[n++] = *p;
  }
  return x - x0;
}

float hint_width(const hint_style *st, const char *s) { return walk(NULL, st, 0, 0, s); }
void hint_paint(cv *c, const hint_style *st, float x, float cy, const char *s) { walk(c, st, x, cy, s); }

const char *hint_pick(const char *pad, const char *keys, const char *mouse, const char *phone) {
  switch (input_last_dev) {
    case DEV_KEYS: return keys ? keys : pad;
    case DEV_MOUSE: return mouse ? mouse : keys ? keys : pad;
    case DEV_PHONE: return phone ? phone : pad;
    default: return pad;
  }
}

void hint_line_set(hint_line *h, const hint_style *st, const char *s) {
  if (!s) s = "";
  if (h->L.c && !strcmp(h->s, s) && !memcmp(&h->st, st, sizeof *st)) return;
  snprintf(h->s, sizeof h->s, "%s", s);
  h->st = *st;
  h->w = ceilf(hint_width(st, s)) + 4;
  h->h = hint_h(st->fs) + 4;
  L_ensure(&h->L, (int)h->w + 2, (int)h->h + 2);
  if (*s) hint_paint(h->L.c, st, 1, 1 + h->h / 2, s);
}
void hint_line_draw(hint_line *h, float cx, float cy, float alpha) {
  if (!h->L.c || !h->s[0]) return;
  L_upload(&h->L);
  ov_draw(h->L.t, roundf(cx - h->w / 2), roundf(cy - h->h / 2 - 1), alpha);
}

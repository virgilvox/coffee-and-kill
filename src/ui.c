/* ui.c: the page's HUD and the Rr class (see ui.h): the text engine, the CSS
 * box helpers, the HUD elements and their transitions and keyframes, and the
 * public calls. The screens are ui_screens.c.
 *
 * Every number below is the page's CSS resolved at a 1280 x 720 viewport and
 * checked against Chrome's layout of the page (element boxes and text line
 * boxes read over the DevTools protocol). */
#include "ui_int.h"
#include "ck.h"
#include "audio.h"
#include "input.h"
#include "ui_kern.h"
#include "ui_mark.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

/* weak: stubs.c (the parent's link stand-ins) defines the same zeroed objects while it is in the build */
__attribute__((weak)) ui_callbacks ui_cb;
__attribute__((weak)) ui_online_t ui_online;
__attribute__((weak)) ui_pads_t ui_pads;
void (*ui_on_olweapon)(const char *w);
void (*ui_on_challenge)(const char *peer_id);
void (*ui_on_room_enter)(void);
float ui_now;
bool ui_test_no_grain;   /* the test driver's comparisons with the page (whose grain it hides) */

/* ================================================================== text */

static const float ASC[3] = {0.878f, 1.025f, 1.020f}, DESC[3] = {0.210f, 0.275f, 0.300f};
static cv *meas;           /* a 1 x 1 canvas for glyph advances */

float t_asc(int f, float fs) { return roundf(ASC[f] * fs); }
float t_desc(int f, float fs) { return roundf(DESC[f] * fs); }
float t_line(int f, float fs) { return t_asc(f, fs) + t_desc(f, fs); }
float t_base(int f, float fs, float top, float lh) { return top + floorf((lh - t_line(f, fs)) / 2) + t_asc(f, fs); }

static int kern(unsigned char a, unsigned char b) {
  int lo = 0, hi = (int)KERN_N - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    int d = KERN[m].a != a ? (int)KERN[m].a - a : (int)KERN[m].b - b;
    if (d == 0) return KERN[m].v;
    if (d < 0) lo = m + 1; else hi = m - 1;
  }
  return 0;
}

/* the advance of one glyph at fs, cached per font for ASCII at a reference size */
static float adv_cache[3][128]; static bool adv_ok[3];
static float adv(int f, float fs, unsigned char ch) {
  if (!adv_ok[f]) {
    cv_font(meas, f, 1000);
    for (int i = 32; i < 127; i++) { char g[2] = {(char)i, 0}; adv_cache[f][i] = cv_measure(meas, g) / 1000.f; }
    adv_ok[f] = true;
  }
  if (ch < 32 || ch > 126) ch = '?';
  return adv_cache[f][ch] * fs;
}

float t_width(int f, float fs, float ls, const char *s) {
  float w = 0; unsigned char prev = 0;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (f == F_MARKER && prev) w += kern(prev, *p) * fs / 1000.f;
    w += adv(f, fs, *p) + ls;
    prev = *p;
  }
  return w;
}

void t_draw(cv *c, int f, float fs, float ls, float x, float base, const char *s, col_t col) {
  cv_fill_hex(c, col.rgb, col.a);
  cv_font(c, f, fs);
  cv_letter_spacing(c, 0);
  cv_text_align(c, CV_LEFT);
  cv_text_baseline(c, CV_ALPHABETIC);
  unsigned char prev = 0;
  char g[2] = {0, 0};
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (f == F_MARKER && prev) x += kern(prev, *p) * fs / 1000.f;
    if (*p != ' ') { g[0] = (char)*p; cv_fill_text(c, g, x, base); }
    x += adv(f, fs, *p) + ls;
    prev = *p;
  }
}

void t_draw_sh(cv *c, int f, float fs, float ls, float x, float base, const char *s, col_t col, const tshadow *sh) {
  if (sh) for (int i = sh->n - 1; i >= 0; i--) t_draw(c, f, fs, ls, x + sh->s[i].dx, base + sh->s[i].dy, s, sh->s[i].c);
  t_draw(c, f, fs, ls, x, base, s, col);
}

void t_collapse(char *dst, const char *src, int cap) {
  int n = 0; bool sp = true;
  for (const char *p = src; *p && n < cap - 1; p++) {
    if (*p == ' ' || *p == '\n' || *p == '\t') { if (!sp) { dst[n++] = ' '; sp = true; } }
    else { dst[n++] = *p; sp = false; }
  }
  while (n > 0 && dst[n - 1] == ' ') n--;
  dst[n] = 0;
}

int t_wrap(int f, float fs, float ls, const char *s, float w, bool break_all, char lines[][160], int max) {
  int n = 0; const char *p = s;
  while (*p && n < max) {
    /* the longest prefix that fits (break_all: any character; else at a space) */
    int best = 0, i = 0; char buf[160];
    while (p[i] && i < 159) {
      memcpy(buf, p, i + 1); buf[i + 1] = 0;
      /* a trailing space hangs (does not count) */
      int k = i + 1; while (k > 0 && buf[k - 1] == ' ') k--;
      buf[k] = 0;
      if (t_width(f, fs, ls, buf) > w + 0.01f) break;
      if (break_all || p[i + 1] == ' ' || p[i + 1] == 0) best = i + 1;
      i++;
    }
    if (best == 0) { /* an unbreakable word wider than the line: it overflows */
      best = 0; while (p[best] && p[best] != ' ') best++;
      if (break_all || best == 0) best = i > 0 ? i : 1;
    }
    memcpy(lines[n], p, best); lines[n][best] = 0;
    int k = best; while (k > 0 && lines[n][k - 1] == ' ') k--; lines[n][k] = 0;
    n++;
    p += best; while (*p == ' ') p++;
  }
  return n;
}

/* ================================================================== boxes */

void c_rect(cv *c, float x, float y, float w, float h, col_t col) {
  if (w <= 0 || h <= 0 || col.a <= 0) return;
  /* Chrome paints boxes on pixel-snapped edges (in the element's own space) */
  float x0 = roundf(x), y0 = roundf(y), x1 = roundf(x + w), y1 = roundf(y + h);
  if (x1 <= x0 || y1 <= y0) return;
  cv_fill_hex(c, col.rgb, col.a); cv_fill_rect(c, x0, y0, x1 - x0, y1 - y0);
}
void c_border(cv *c, float x, float y, float w, float h, float bw, col_t col) {
  if (bw <= 0) return;
  c_rect(c, x, y, w, bw, col); c_rect(c, x, y + h - bw, w, bw, col);
  c_rect(c, x, y + bw, bw, h - 2 * bw, col); c_rect(c, x + w - bw, y + bw, bw, h - 2 * bw, col);
}
void c_box(cv *c, float x, float y, float w, float h, col_t bg, float bw, col_t bc, float sdx, float sdy, col_t sc) {
  if (sc.a > 0 && (sdx || sdy)) c_rect(c, x + sdx, y + sdy, w, h, sc);
  c_rect(c, x, y, w, h, bg);
  c_border(c, x, y, w, h, bw, bc);
}
/* StrokeData's dash fit: dashes 3 x width, gaps near 2 x width, a dash at both ends */
static void dash_run(cv *c, float x, float y, float len, float bw, bool horiz, col_t col) {
  float dash = bw * 3, gap = bw * 2;
  if (len <= dash) { if (horiz) c_rect(c, x, y, len, bw, col); else c_rect(c, x, y, bw, len, col); return; }
  float nmin = floorf((len + gap) / (dash + gap)), nmax = nmin + 1;
  float gmin = nmin > 1 ? (len - nmin * dash) / (nmin - 1) : 0, gmax = (len - nmax * dash) / (nmax - 1);
  float n = (gmax <= 0 || fabsf(gmin - gap) < fabsf(gmax - gap)) ? nmin : nmax;
  float g = n > 1 ? (len - n * dash) / (n - 1) : 0;
  for (int i = 0; i < (int)n; i++) {
    float o = i * (dash + g);
    if (horiz) c_rect(c, x + o, y, dash, bw, col); else c_rect(c, x, y + o, bw, dash, col);
  }
}
void c_dash_h(cv *c, float x, float y, float w, float bw, col_t col) { dash_run(c, x, y, w, bw, true, col); }
void c_dash_rect(cv *c, float x, float y, float w, float h, float bw, col_t col) {
  dash_run(c, x, y, w, bw, true, col); dash_run(c, x, y + h - bw, w, bw, true, col);
  dash_run(c, x, y, h, bw, false, col); dash_run(c, x + w - bw, y, h, bw, false, col);
}

void L_ensure(layer *l, int w, int h) {
  if (!l->c || l->w != w || l->h != h) {
    if (l->c) cv_free(l->c);
    if (l->t) ov_tex_free(l->t);
    l->c = cv_new(w, h); l->t = ov_tex_new(w, h); l->w = w; l->h = h;
  } else cv_clear(l->c);
  cv_reset_transform(l->c);
  l->dirty = true;
}
void L_upload(layer *l) { if (l->c && l->dirty) { ov_tex_from_cv(l->t, l->c); l->dirty = false; } }

/* ================================================================== easing */

float ease_bezier(float x1, float y1, float x2, float y2, float t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  /* solve x(u) = t by Newton then bisection, return y(u) */
  float u = t;
  for (int i = 0; i < 8; i++) {
    float x = ((1 - 3 * x2 + 3 * x1) * u + (3 * x2 - 6 * x1)) * u * u + 3 * x1 * u;
    float dx = (3 * (1 - 3 * x2 + 3 * x1) * u + 2 * (3 * x2 - 6 * x1)) * u + 3 * x1;
    if (fabsf(x - t) < 1e-5f) break;
    if (fabsf(dx) < 1e-6f) break;
    u -= (x - t) / dx;
  }
  if (u < 0 || u > 1) {
    float lo = 0, hi = 1; u = t;
    for (int i = 0; i < 30; i++) {
      float x = ((1 - 3 * x2 + 3 * x1) * u + (3 * x2 - 6 * x1)) * u * u + 3 * x1 * u;
      if (x < t) lo = u; else hi = u;
      u = (lo + hi) / 2;
    }
  }
  return ((1 - 3 * y2 + 3 * y1) * u + (3 * y2 - 6 * y1)) * u * u + 3 * y1 * u;
}

static float tr_val(const trans_t *tr, float now) {
  float t = now - tr->start - tr->delay;
  if (t <= 0) return tr->from;
  if (tr->dur <= 0 || t >= tr->dur) return tr->to;
  return tr->from + (tr->to - tr->from) * EASE(t / tr->dur);
}
void tr_set(trans_t *tr, float v, float now, float dur, float delay) {
  if (!tr->init) { tr->init = true; tr->from = tr->to = v; tr->start = now; tr->dur = dur; tr->delay = delay; return; }
  if (v == tr->to) return;
  tr->from = tr_val(tr, now); tr->to = v; tr->start = now; tr->dur = dur; tr->delay = delay;
}
void tr_jump(trans_t *tr, float v) { tr->init = true; tr->from = tr->to = v; tr->dur = 0; tr->delay = 0; }
float tr_get(const trans_t *tr, float now) { return tr_val(tr, now); }
bool tr_busy(const trans_t *tr, float now) { return now < tr->start + tr->delay + tr->dur && tr->from != tr->to; }

col_t col_mix(col_t a, col_t b, float t) {
  /* CSS interpolates colours as premultiplied sRGB */
  float aa = a.a + (b.a - a.a) * t;
  float ch[3];
  for (int i = 0; i < 3; i++) {
    float ca = ((a.rgb >> (16 - 8 * i)) & 255) * a.a, cb = ((b.rgb >> (16 - 8 * i)) & 255) * b.a;
    float v = ca + (cb - ca) * t;
    ch[i] = aa > 0 ? v / aa : 0;
  }
  uint32_t rgb = ((uint32_t)fminf(255, roundf(ch[0])) << 16) | ((uint32_t)fminf(255, roundf(ch[1])) << 8) | (uint32_t)fminf(255, roundf(ch[2]));
  return (col_t){rgb, aa};
}

/* ================================================================== the mark */

uint8_t ui_mark_px[MARK_N * MARK_N * 4];
typedef struct { float x0, y0, x1, y1; } seg_t;
static seg_t *segs; static int nseg, segcap;
static void seg_add(float x0, float y0, float x1, float y1) {
  if (nseg == segcap) { segcap = segcap ? segcap * 2 : 1024; segs = realloc(segs, sizeof(seg_t) * segcap); }
  segs[nseg++] = (seg_t){x0, y0, x1, y1};
}
static const char *num(const char *p, float *v) {
  while (*p == ' ' || *p == ',' || *p == '\n') p++;
  char *e; *v = strtof(p, &e); return e;
}
void ui_mark_init(void) {
  /* flatten the path (M m L l H h V v C c S s Z z) to segments in viewBox units */
  char *d = NULL; size_t n = 0;
  for (size_t i = 0; i < sizeof MARK_D / sizeof MARK_D[0]; i++) {
    size_t k = strlen(MARK_D[i]); d = realloc(d, n + k + 1); memcpy(d + n, MARK_D[i], k); n += k; d[n] = 0;
  }
  float cx = 0, cy = 0, sx = 0, sy = 0, lcx = 0, lcy = 0; char cmd = 'M', prev = 0;
  const char *p = d;
  while (*p) {
    while (*p == ' ' || *p == ',' || *p == '\n') p++;
    if (!*p) break;
    if (isalpha((unsigned char)*p)) {
      cmd = *p++;
      if (cmd == 'z' || cmd == 'Z') { if (cx != sx || cy != sy) seg_add(cx, cy, sx, sy); cx = sx; cy = sy; prev = cmd; }
      continue;
    }
    bool rel = islower((unsigned char)cmd);
    char C = (char)toupper((unsigned char)cmd);
    float a, b, c1x, c1y, c2x, c2y, x, y;
    if (C == 'M' || C == 'L') {
      p = num(p, &a); p = num(p, &b);
      x = rel ? cx + a : a; y = rel ? cy + b : b;
      if (C == 'M') { sx = x; sy = y; cmd = rel ? 'l' : 'L'; } else seg_add(cx, cy, x, y);
      cx = x; cy = y;
    } else if (C == 'H') { p = num(p, &a); x = rel ? cx + a : a; seg_add(cx, cy, x, cy); cx = x; }
    else if (C == 'V') { p = num(p, &a); y = rel ? cy + a : a; seg_add(cx, cy, cx, y); cy = y; }
    else if (C == 'C' || C == 'S') {
      if (C == 'C') { p = num(p, &c1x); p = num(p, &c1y); if (rel) { c1x += cx; c1y += cy; } }
      else {
        char pc = (char)toupper((unsigned char)prev);
        if (pc == 'C' || pc == 'S') { c1x = 2 * cx - lcx; c1y = 2 * cy - lcy; } else { c1x = cx; c1y = cy; }
      }
      p = num(p, &c2x); p = num(p, &c2y); p = num(p, &x); p = num(p, &y);
      if (rel) { c2x += cx; c2y += cy; x += cx; y += cy; }
      float px0 = cx, py0 = cy;
      for (int i = 1; i <= 12; i++) {
        float t = i / 12.f, u = 1 - t;
        float qx = u * u * u * cx + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * x;
        float qy = u * u * u * cy + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * y;
        seg_add(px0, py0, qx, qy); px0 = qx; py0 = qy;
      }
      lcx = c2x; lcy = c2y; cx = x; cy = y;
    } else { p++; }
    prev = cmd;
  }
  free(d);
  /* the svg is 34 x 34 css px showing the 1000 x 1000 viewBox, the group translated by (-26, -4):
   * evenodd coverage with 8 x 8 samples a pixel */
  const int SS = 8;
  float sc = 1000.f / MARK_N;
  for (int py = 0; py < MARK_N; py++) for (int pxx = 0; pxx < MARK_N; pxx++) {
    int hit = 0;
    for (int sy2 = 0; sy2 < SS; sy2++) {
      float vy = (py + (sy2 + 0.5f) / SS) * sc + 4;   /* back through translate(-26,-4) */
      for (int sx2 = 0; sx2 < SS; sx2++) {
        float vx = (pxx + (sx2 + 0.5f) / SS) * sc + 26;
        int cross = 0;
        for (int i = 0; i < nseg; i++) {
          seg_t *s = &segs[i];
          if ((s->y0 > vy) != (s->y1 > vy)) {
            float xi = s->x0 + (vy - s->y0) * (s->x1 - s->x0) / (s->y1 - s->y0);
            if (xi > vx) cross++;
          }
        }
        hit += cross & 1;
      }
    }
    uint8_t *q = ui_mark_px + (py * MARK_N + pxx) * 4;
    q[0] = 0xF2; q[1] = 0xAB; q[2] = 0x1E; q[3] = (uint8_t)(hit * 255 / (SS * SS));
  }
  free(segs); segs = NULL; nseg = segcap = 0;
}

/* ================================================================== HUD state */

static const tshadow SH_NM = {4, {{3, 3, COL(C_INK)}, {-1, -1, COL(C_INK)}, {1, -1, COL(C_INK)}, {-1, 1, COL(C_INK)}}};
static const tshadow SH_COMBO = {4, {{4, 4, COL(C_INK)}, {-2, -2, COL(C_INK)}, {2, -2, COL(C_INK)}, {-2, 2, COL(C_INK)}}};
static const tshadow SH_SMALL2 = {1, {{2, 2, COL(C_INK)}}};
static const tshadow SH_ANN = {5, {{8, 8, COL(C_PINK)}, {-3, -3, COL(C_INK)}, {3, -3, COL(C_INK)}, {-3, 3, COL(C_INK)}, {3, 3, COL(C_INK)}}};
static const tshadow SH_ANN_SMALL = {1, {{3, 3, COL(C_INK)}}};
static const tshadow SH_FX = {4, {{3, 3, COL(C_INK)}, {-1, -1, COL(C_INK)}, {1, -1, COL(C_INK)}, {-1, 1, COL(C_INK)}}};
static const tshadow SH_HINT = {1, {{1, 1, COL(C_INK)}}};

#define NFX 16
static struct {
  bool on;
  char nm[2][32], pn[2][8];
  trans_t hp[2], lag[2];
  float meter[2]; float full_t0[2]; bool full[2];
  int pips_on[2], pips_of[2];
  float painted[2][6]; bool side_ok[2];
  layer side[2];
  char timer_s[8]; bool low; float low_t0; layer timerL; float timer_w; bool timer_ok;
  char round[32]; layer roundL; float round_w; bool round_ok;
  struct { bool show; trans_t op; char big[24], small[48]; layer L; float w, h; bool ok; } combo[2];
  struct { char big[64], small[96]; int mode; float t0; layer L; float h; int nl; } ann;
  struct { char s[96]; bool show; trans_t op; float until; layer L; float w; } toast;
  struct { bool live; float t0, x, y; layer L; float w, h; } fx[NFX];
  char hint[200]; layer hintL; float hint_w;
  uint32_t flash_rgb; float flash_a0, flash_t0, flash_dur;
} H;

/* ---- the two .side blocks (nameplate, bar, meter, pips) ---- */
#define SIDE_W 560
#define SIDE_H 150
static float side_x0(int i) { return i == 0 ? 0 : CK_W - SIDE_W; }   /* the canvas's page x */

static void paint_side(int i, float hp, float lag, float bright) {
  layer *L = &H.side[i];
  L_ensure(L, SIDE_W, SIDE_H);
  cv *c = L->c;
  float ox = side_x0(i);
  cv_set_transform(c, 1, 0, 0, 1, -ox, 0);   /* draw in page coordinates */
  float sx = i == 0 ? 22 : 746, top = 18;     /* .side: top 18, left 22 / right 22, width 512 */
  /* nameplate: .pn (lcd 26, line-height 1.1, padding 0 8, border 3) and .nm (marker 40, ls .02em), baseline aligned, gap 10 */
  const char *pn = H.pn[i], *nm = H.nm[i];
  float pnw = t_width(F_LCD, 26, 0, pn) + 16 + 6;
  float nmw = t_width(F_MARKER, 40, 0.8f, nm);
  float base = top + t_asc(F_MARKER, 40);       /* the line's baseline: the name is the tallest */
  float pn_top = base - t_asc(F_LCD, 26) - floorf((28.6f - t_line(F_LCD, 26)) / 2) - 3;   /* box top so its text baseline meets */
  float pnx, nmx;
  if (i == 0) { pnx = sx; nmx = sx + pnw + 10; }
  else { pnx = sx + 512 - pnw; nmx = pnx - 10 - nmw; }
  c_box(c, pnx, pn_top, pnw, 28.6f + 6, COL(C_CREAM), 3, COL(C_INK), 0, 0, COL(0));
  t_draw(c, F_LCD, 26, 0, pnx + 11, base, pn, COL(C_INK));
  t_draw_sh(c, F_MARKER, 40, 0.8f, nmx, base, nm, COL(C_CREAM), &SH_NM);

  /* .bar: 512 x 30 at top 65.59, skewX(-14deg) (p2 +14) about its centre, border 4, shadow 5 5, overflow hidden */
  float by = top + 47.59f, k = tanf(14 * CK_PI / 180) * (i == 0 ? 1 : -1);
  float cy = by + 15;
  cv_set_transform(c, 1, 0, -k, 1, -ox + k * cy, 0);
  c_rect(c, sx + 5, by + 5, 512, 30, COL(C_INK));         /* box-shadow */
  c_rect(c, sx, by, 512, 30, COL(C_VOID));
  float iw = 504, ix = sx + 4, iy = by + 4, ih = 22;
  cv_save(c);
  cv_begin(c); cv_rect(c, ix, iy, iw, ih); cv_clip(c);
  /* outline: 2px cream, offset -6; Chrome paints it under the positioned .lag, .fill and ::after */
  c_border(c, sx + 4, by + 4, 504, 22, 2, COL(C_CREAM));
  float lw = iw * fmaxf(0, lag) / 100.f, fw = iw * fmaxf(0, hp) / 100.f;
  float lx = i == 0 ? ix : ix + iw - lw, fx = i == 0 ? ix : ix + iw - fw;
  c_rect(c, lx, iy, lw, ih, COL(C_PINK));
  if (fw > 0) {
    float g0 = roundf(iy), g1 = roundf(iy + ih);
    cv_fill_linear(c, 0, g0, 0, g1);
    if (i == 0) { cv_fill_stop(c, 0, "#f7c657"); cv_fill_stop(c, 0.6f, "#f2ab1e"); cv_fill_stop(c, 1, "#c98714"); }
    else { cv_fill_stop(c, 0, "#9cc2e6"); cv_fill_stop(c, 0.6f, "#5e93c4"); cv_fill_stop(c, 1, "#35648f"); }
    float f0 = roundf(fx), f1 = roundf(fx + fw);
    cv_fill_rect(c, f0, g0, f1 - f0, g1 - g0);
  }
  /* ::after: repeating-linear-gradient(90deg, transparent 0 38px, rgba(0,0,0,.45) 38px 41px) */
  for (float x = ix + 38; x < ix + iw; x += 41) c_rect(c, x, iy, fminf(3, ix + iw - x), ih, COLA(0, 0.45f));
  cv_restore(c);
  c_border(c, sx, by, 512, 30, 4, COL(C_INK));

  /* .meter: 72% wide, 12 tall at margin-top 11, border 3, skewX(-14deg) (p2 +14), overflow hidden */
  float mw = 512 * 0.72f, mx = i == 0 ? sx : sx + 512 - mw, my = by + 30 + 11, mcy = my + 6;
  cv_set_transform(c, 1, 0, -k, 1, -ox + k * mcy, 0);
  c_rect(c, mx, my, mw, 12, COL(C_VOID));
  float miw = mw - 6, mfw = miw * fminf(100, H.meter[i]) / 100.f;
  float mix = mx + 3, miy = my + 3;
  if (mfw > 0) {
    col_t f = H.full[i] ? COL(C_CREAM) : COL(C_PINK);
    if (H.full[i] && bright > 1) {   /* filter: brightness() */
      uint32_t r = (uint32_t)fminf(255, ((f.rgb >> 16) & 255) * bright), g = (uint32_t)fminf(255, ((f.rgb >> 8) & 255) * bright),
               b = (uint32_t)fminf(255, (f.rgb & 255) * bright);
      f.rgb = (r << 16) | (g << 8) | b;
    }
    c_rect(c, i == 0 ? mix : mix + miw - mfw, miy, mfw, 6, f);
  }
  if (H.full[i]) {   /* the label, clipped to the 6 px padding box */
    cv_save(c);
    cv_begin(c); cv_rect(c, mix, miy, miw, 6); cv_clip(c);
    const char *lab = "EXTREME READY (F)";
    float tw = t_width(F_LCD, 13, 0, lab);
    t_draw(c, F_LCD, 13, 0, mix + (miw - tw) / 2, t_base(F_LCD, 13, miy, 12), lab, COL(C_INK));
    cv_restore(c);
  }
  c_border(c, mx, my, mw, 12, 3, COL(C_INK));

  /* .pips: 22 x 16, gap 7, margin-top 9, border 3 cream, skewX(-14deg) each (both sides) */
  float py = my + 12 + 9;
  int of = H.pips_of[i];
  float total = of * 22 + (of > 0 ? (of - 1) * 7 : 0);
  float px0 = i == 0 ? sx : sx + 512 - total;
  float kp = tanf(14 * CK_PI / 180);
  for (int p = 0; p < of; p++) {
    float x = px0 + p * 29, pcy = py + 8;
    cv_set_transform(c, 1, 0, -kp, 1, -ox + kp * pcy, 0);
    if (p < H.pips_on[i]) c_rect(c, x, py, 22, 16, COL(i == 0 ? C_PINK : C_CYAN));
    c_border(c, x, py, 22, 16, 3, COL(C_CREAM));
  }
  cv_reset_transform(c);
}

/* ---- the centre: timer and round label ---- */
static void paint_timer(void) {
  float tw = t_width(F_LCD, 72, 0, H.timer_s);
  float w = fmaxf(96, tw + 32 + 8);
  H.timer_w = w;
  layer *L = &H.timerL;
  L_ensure(L, 200, 100);
  cv *c = L->c;
  /* the box at (4, 4) in the canvas; shadow 5 5 */
  c_box(c, 4, 4, w, 82, COL(C_VOID), 4, COL(C_PINK), 5, 5, COL(C_INK));
  col_t col = H.low ? COL(C_ERR) : COL(C_CREAM);
  t_draw(c, F_LCD, 72, 0, 4 + (w - tw) / 2, t_base(F_LCD, 72, 4 + 4 + 2, 72), H.timer_s, col);
  H.timer_ok = true;
}
static void paint_round(void) {
  float tw = t_width(F_TYPE, 15, 4.5f, H.round);
  float w = tw + 20 + 6;
  H.round_w = w;
  layer *L = &H.roundL;
  L_ensure(L, (int)w + 8, 34);
  cv *c = L->c;
  c_box(c, 0, 0, w, 29, COL(C_CREAM), 3, COL(C_INK), 0, 0, COL(0));
  t_draw(c, F_TYPE, 15, 4.5f, 13, t_base(F_TYPE, 15, 5, t_line(F_TYPE, 15)), H.round, COL(C_INK));
  H.round_ok = true;
}

/* ---- combos ---- */
static void paint_combo(int i) {
  float fs = 56.32f, sfs = fs * 0.32f, sls = sfs * 0.25f;
  float w1 = t_width(F_MARKER, fs, 0, H.combo[i].big), w2 = H.combo[i].small[0] ? t_width(F_TYPE, sfs, sls, H.combo[i].small) : 0;
  float w = fmaxf(w1, w2);
  float h = t_line(F_MARKER, fs) + (H.combo[i].small[0] ? t_line(F_TYPE, sfs) : 0);   /* an empty <small> has no line box */
  H.combo[i].w = w; H.combo[i].h = h;
  layer *L = &H.combo[i].L;
  L_ensure(L, (int)ceilf(w) + 24, (int)ceilf(h) + 24);
  cv *c = L->c;
  float ox = 12, oy = 12;
  col_t col = COL(i == 0 ? C_PINK : C_CYAN);
  float b1 = oy + t_asc(F_MARKER, fs);
  float b2 = oy + t_line(F_MARKER, fs) + t_asc(F_TYPE, sfs);
  t_draw_sh(c, F_MARKER, fs, 0, ox + (i == 0 ? 0 : w - w1), b1, H.combo[i].big, col, &SH_COMBO);
  if (H.combo[i].small[0]) t_draw_sh(c, F_TYPE, sfs, sls, ox + (i == 0 ? 0 : w - w2), b2, H.combo[i].small, COL(C_CREAM), &SH_SMALL2);
  H.combo[i].ok = true;
}

/* ---- announce ---- */
#define ANN_FS 153.6f
static void paint_announce(void) {
  char lines[4][160];
  int nl = t_wrap(F_MARKER, ANN_FS, 0, H.ann.big, CK_W, false, lines, 4);
  float lh = ANN_FS * 0.95f;
  float sfs = ANN_FS * 0.17f, sls = sfs * 0.35f, slh = sfs * 0.95f;
  char sl[4][160]; int ns = H.ann.small[0] ? t_wrap(F_TYPE, sfs, sls, H.ann.small, CK_W, false, sl, 4) : 0;
  float h = nl * lh + (ns ? 14 + ns * slh : 0);
  H.ann.h = h; H.ann.nl = nl;
  layer *L = &H.ann.L;
  int pad = 40;
  L_ensure(L, CK_W + 2 * pad, (int)ceilf(h) + 2 * pad);
  cv *c = L->c;
  for (int k = 0; k < nl; k++) {
    float w = t_width(F_MARKER, ANN_FS, 0, lines[k]);
    t_draw_sh(c, F_MARKER, ANN_FS, 0, pad + (CK_W - w) / 2, pad + t_base(F_MARKER, ANN_FS, k * lh, lh), lines[k], COL(C_CREAM), &SH_ANN);
  }
  for (int k = 0; k < ns; k++) {
    float w = t_width(F_TYPE, sfs, sls, sl[k]);
    t_draw_sh(c, F_TYPE, sfs, sls, pad + (CK_W - w) / 2, pad + t_base(F_TYPE, sfs, nl * lh + 14 + k * slh, slh), sl[k], COL(C_CREAM), &SH_ANN_SMALL);
  }
}

/* @keyframes pop over 1.15 s, cubic-bezier(.2,1.4,.4,1) per interval:
 * opacity 0 (0%) 1 (18%) 1 (80%) 0 (100%); transform scale(.4) rotate(-9deg) (0%),
 * scale(1.12) rotate(-3deg) (18%), scale(1) rotate(-3deg) (30%), scale(1.15) rotate(-3deg) (100%) */
static float kf(const float *t, const float *v, int n, float p) {
  if (p <= t[0]) return v[0];
  for (int i = 0; i < n - 1; i++) if (p <= t[i + 1]) {
    float u = (p - t[i]) / (t[i + 1] - t[i]);
    return v[i] + (v[i + 1] - v[i]) * ease_bezier(.2f, 1.4f, .4f, 1, u);
  }
  return v[n - 1];
}
static void announce_state(float *op, float *sc, float *rot) {
  if (H.ann.mode == 0) { *op = 0; *sc = 0.6f; *rot = -3; return; }
  if (H.ann.mode == 2) { *op = 1; *sc = 1; *rot = -3; return; }
  float p = (ui_now - H.ann.t0) / 1.15f;
  if (p > 1) p = 1;
  static const float ot[4] = {0, .18f, .8f, 1}, ov[4] = {0, 1, 1, 0};
  static const float tt[4] = {0, .18f, .3f, 1}, sv[4] = {.4f, 1.12f, 1, 1.15f}, rv[4] = {-9, -3, -3, -3};
  *op = kf(ot, ov, 4, p); *sc = kf(tt, sv, 4, p); *rot = kf(tt, rv, 4, p);
  if (*op < 0) *op = 0;
  if (*op > 1) *op = 1;
}

/* ---- toast, hint, fx ---- */
static void paint_toast(void) {
  float tw = t_width(F_LCD, 34, 0, H.toast.s);
  float w = tw + 28 + 6;
  H.toast.w = w;
  L_ensure(&H.toast.L, (int)ceilf(w) + 2, 56);
  cv *c = H.toast.L.c;
  c_box(c, 0, 0, w, 55, COL(C_INK), 3, COL(C_PINK), 0, 0, COL(0));
  t_draw(c, F_LCD, 34, 0, 17, t_base(F_LCD, 34, 5, t_line(F_LCD, 34)), H.toast.s, COL(C_CREAM));
}
static void paint_hint(void) {
  float tw = t_width(F_TYPE, 13, 1.82f, H.hint);
  H.hint_w = tw;
  L_ensure(&H.hintL, (int)ceilf(tw) + 4, 20);
  if (H.hint[0]) t_draw_sh(H.hintL.c, F_TYPE, 13, 1.82f, 0, t_asc(F_TYPE, 13), H.hint, COLA(0xf5f0e6, 0.75f), &SH_HINT);
}

/* ================================================================== API */

void ui_init(void) {
  meas = cv_new(1, 1);
  memset(&H, 0, sizeof H);
  strcpy(H.nm[0], "MOHEEB"); strcpy(H.nm[1], "LUIS");
  strcpy(H.pn[0], "P1"); strcpy(H.pn[1], "P2");
  for (int i = 0; i < 2; i++) { tr_jump(&H.hp[i], 100); tr_jump(&H.lag[i], 100); tr_jump(&H.combo[i].op, 0); }
  tr_jump(&H.toast.op, 0);
  strcpy(H.timer_s, "60"); strcpy(H.round, "ROUND 1");
  ui_mark_init();
  scr_init();
}

void ui_set_names(const char *n0, const char *n1, const char *pn0, const char *pn1) {
  snprintf(H.nm[0], sizeof H.nm[0], "%s", n0); snprintf(H.nm[1], sizeof H.nm[1], "%s", n1);
  for (char *p = H.nm[0]; *p; p++) *p = (char)toupper((unsigned char)*p);   /* text-transform: uppercase */
  for (char *p = H.nm[1]; *p; p++) *p = (char)toupper((unsigned char)*p);
  if (pn0) snprintf(H.pn[0], sizeof H.pn[0], "%s", pn0);
  if (pn1) snprintf(H.pn[1], sizeof H.pn[1], "%s", pn1);
  H.side_ok[0] = H.side_ok[1] = false;
}
void ui_set_health(int i, float pct, bool instant) {
  if (i < 0 || i > 1) return;
  float t = fmaxf(0, pct);
  tr_set(&H.hp[i], t, ui_now, 0.08f, 0);
  if (instant) tr_jump(&H.lag[i], t);
  else tr_set(&H.lag[i], t, ui_now, 0.5f, 0.35f);
}
void ui_set_meter(int i, float pct) {
  if (i < 0 || i > 1) return;
  if (H.meter[i] != pct) { H.meter[i] = pct; H.side_ok[i] = false; }
  bool f = pct >= 100;
  if (f != H.full[i]) { H.full[i] = f; H.full_t0[i] = ui_now; H.side_ok[i] = false; }
}
void ui_set_pips(int i, int on, int of) {
  if (i < 0 || i > 1) return;
  if (H.pips_on[i] != on || H.pips_of[i] != of) { H.pips_on[i] = on; H.pips_of[i] = of; H.side_ok[i] = false; }
}
void ui_set_timer(float t) {
  char s[8]; snprintf(s, sizeof s, "%d", (int)ceilf(fmaxf(0, t)));
  bool low = t <= 10;
  if (low != H.low) { H.low = low; H.low_t0 = ui_now; H.timer_ok = false; }
  if (strcmp(s, H.timer_s)) { strcpy(H.timer_s, s); H.timer_ok = false; }
}
void ui_set_round(const char *s) { if (strcmp(s, H.round)) { snprintf(H.round, sizeof H.round, "%s", s); H.round_ok = false; } }
void ui_set_hint(const char *s) {
  char t[200]; t_collapse(t, s ? s : "", sizeof t);
  if (strcmp(t, H.hint)) { strcpy(H.hint, t); paint_hint(); }
}
void ui_combo(int i, int n, const char *small) {
  if (i < 0 || i > 1) return;
  if (n >= 2) {
    char big[24]; snprintf(big, sizeof big, "%d HITS", n);
    const char *sm = small ? small : "";
    if (strcmp(big, H.combo[i].big) || strcmp(sm, H.combo[i].small)) {
      strcpy(H.combo[i].big, big); snprintf(H.combo[i].small, sizeof H.combo[i].small, "%s", sm); H.combo[i].ok = false;
    }
    if (!H.combo[i].show) { H.combo[i].show = true; tr_set(&H.combo[i].op, 1, ui_now, 0.2f, 0); }
  } else if (H.combo[i].show) { H.combo[i].show = false; tr_set(&H.combo[i].op, 0, ui_now, 0.2f, 0); }
}
void ui_announce(const char *big, const char *small, bool stay) {
  t_collapse(H.ann.big, big ? big : "", sizeof H.ann.big);
  t_collapse(H.ann.small, small ? small : "", sizeof H.ann.small);
  H.ann.mode = stay ? 2 : 1; H.ann.t0 = ui_now;
  paint_announce();
}
void ui_clear_announce(void) { H.ann.mode = 0; H.ann.big[0] = H.ann.small[0] = 0; }
void ui_toast(const char *s) {
  char t[96]; t_collapse(t, s, sizeof t);
  strcpy(H.toast.s, t); paint_toast();
  if (!H.toast.show) { H.toast.show = true; tr_set(&H.toast.op, 1, ui_now, 0.15f, 0); }
  H.toast.until = ui_now + 1.4f;
}
void ui_fx(const char *text, float x, float y, const char *cls) {
  int k = -1;
  for (int i = 0; i < NFX; i++) if (!H.fx[i].live) { k = i; break; }
  if (k < 0) { float old = 1e9f; for (int i = 0; i < NFX; i++) if (H.fx[i].t0 < old) { old = H.fx[i].t0; k = i; } }
  float fs = 30; col_t col = COL(C_CREAM);
  if (cls && !strcmp(cls, "big")) { fs = 44; col = COL(C_PINK); }
  else if (cls && !strcmp(cls, "blk")) col = COL(C_CYAN);
  else if (cls && !strcmp(cls, "dg")) col = COL(C_SLIME);
  float w = t_width(F_MARKER, fs, 0, text), h = t_line(F_MARKER, fs);
  H.fx[k].live = true; H.fx[k].t0 = ui_now; H.fx[k].x = x; H.fx[k].y = y; H.fx[k].w = w; H.fx[k].h = h;
  L_ensure(&H.fx[k].L, (int)ceilf(w) + 16, (int)ceilf(h) + 16);
  t_draw_sh(H.fx[k].L.c, F_MARKER, fs, 0, 8, 8 + t_asc(F_MARKER, fs), text, col, &SH_FX);
}
void ui_flash(uint32_t rgb, float a, float ms) { H.flash_rgb = rgb; H.flash_a0 = a; H.flash_t0 = ui_now; H.flash_dur = ms / 1000.f; }
void ui_hud(bool on) { H.on = on; }

/* ================================================================== frame and draw */

void ui_frame(float dt) {
  ui_now += dt;
  if (H.toast.show && ui_now >= H.toast.until) { H.toast.show = false; tr_set(&H.toast.op, 0, ui_now, 0.15f, 0); }
  for (int i = 0; i < NFX; i++) if (H.fx[i].live && ui_now - H.fx[i].t0 > 0.85f) H.fx[i].live = false;
  scr_frame();
  /* keys and the mouse go to the shown screen (Rr.onKey ignores keys without one) */
  const int *ev; int n = input_key_events(&ev);
  for (int i = 0; i < n; i++) ui_key(ev[i]);
  const ck_pointer *p = input_pointer();
  if (p->active) ui_pointer(p->x, p->y, p->pressed);
}

static void draw_hud(void) {
  /* the sides: repaint when what they show moved */
  for (int i = 0; i < 2; i++) {
    float hp = tr_get(&H.hp[i], ui_now), lag = tr_get(&H.lag[i], ui_now);
    float bright = 1;
    if (H.full[i]) {   /* pulse .5s infinite alternate (ease): brightness 1 -> 1.5 */
      float t = (ui_now - H.full_t0[i]) / 0.5f; int k = (int)floorf(t); float u = t - k;
      if (k & 1) u = 1 - u;
      bright = 1 + 0.5f * EASE(u);
      if (k & 1) bright = 1 + 0.5f * (1 - EASE(1 - u));
    }
    float sig[6] = {hp, lag, H.meter[i], bright, (float)H.pips_on[i], (float)H.pips_of[i]};
    if (!H.side_ok[i] || memcmp(sig, H.painted[i], sizeof sig)) {
      paint_side(i, hp, lag, bright);
      memcpy(H.painted[i], sig, sizeof sig); H.side_ok[i] = true;
    }
    L_upload(&H.side[i]);
    ov_draw(H.side[i].t, side_x0(i), 0, 1);
  }
  /* .center: left 50%, translateX(-50%), top 14 */
  if (!H.timer_ok) paint_timer();
  if (!H.round_ok) paint_round();
  L_upload(&H.timerL); L_upload(&H.roundL);
  float cw = fmaxf(H.timer_w, H.round_w), cx = 640 - cw / 2;
  float top = 1;
  if (H.low) {   /* blink .5s steps(2) infinite: 1, .775, .55, .775 by quarter period */
    float t = fmodf(ui_now - H.low_t0, 0.5f);
    int q = (int)(t / 0.125f);
    static const float lv[4] = {1, 0.775f, 0.55f, 0.775f};
    top = lv[q & 3];
  }
  ov_draw(H.timerL.t, cx + (cw - H.timer_w) / 2 - 4, 14 - 4, top);
  ov_draw(H.roundL.t, cx + (cw - H.round_w) / 2, 14 + 82 + 8, 1);
  /* combos: top 150, left 30 / right 30, rotate(-6deg) / (6deg) about the box centre */
  for (int i = 0; i < 2; i++) {
    float op = tr_get(&H.combo[i].op, ui_now);
    if (op <= 0 || !H.combo[i].big[0]) continue;
    if (!H.combo[i].ok) paint_combo(i);
    L_upload(&H.combo[i].L);
    float w = H.combo[i].w, h = H.combo[i].h;
    float x = i == 0 ? 30 : CK_W - 30 - w, y = 150;
    float m[6]; ov_matrix(m, x - 12, y - 12, 12 + w / 2, 12 + h / 2, (i == 0 ? -6 : 6) * CK_PI / 180, 1, 1, 0, 0);
    ov_draw_m(H.combo[i].L.t, m, op, 1);
  }
  /* #announce: top 36%, full width, the box's centre the origin */
  if (H.ann.mode) {
    float op, sc, rot; announce_state(&op, &sc, &rot);
    L_upload(&H.ann.L);
    float y = 0.36f * CK_H;
    float m[6]; ov_matrix(m, -40, y - 40, 40 + 640, 40 + H.ann.h / 2, rot * CK_PI / 180, sc, sc, 0, 0);
    ov_draw_m(H.ann.L.t, m, op, 1);
  }
  /* #toast: bottom 9%, centred */
  float top_op = tr_get(&H.toast.op, ui_now);
  if (top_op > 0 && H.toast.L.c) {
    L_upload(&H.toast.L);
    ov_draw(H.toast.L.t, 640 - H.toast.w / 2, CK_H - 0.09f * CK_H - 55, top_op);
  }
  /* .hint: bottom 14, centred */
  if (H.hint[0] && H.hintL.c) { L_upload(&H.hintL); ov_draw(H.hintL.t, 640 - H.hint_w / 2, CK_H - 14 - 17, 1); }
  /* .fx: @keyframes fxup .8s ease-out forwards: opacity 0 / 1 (15%) / 0, scale .5 / 1.2 (15%) / 1,
   * margin-top 10 -> -60; transform translate(-50%,-50%) rotate(-6deg); the scale property about the centre */
  for (int i = 0; i < NFX; i++) {
    if (!H.fx[i].live) continue;
    float p = (ui_now - H.fx[i].t0) / 0.8f; if (p > 1) p = 1;
    float op, sc, mt;
    if (p < 0.15f) { float u = EASE_OUT(p / 0.15f); op = u; sc = 0.5f + 0.7f * u; }
    else { float u = EASE_OUT((p - 0.15f) / 0.85f); op = 1 - u; sc = 1.2f - 0.2f * u; }
    mt = 10 - 70 * EASE_OUT(p);
    L_upload(&H.fx[i].L);
    float w = H.fx[i].w, h = H.fx[i].h;
    /* the element box sits at (x, y + mt); origin its centre; scale, then translate(-50%,-50%) rotate(-6deg) */
    float c = cosf(-6 * CK_PI / 180), s = sinf(-6 * CK_PI / 180);
    float ox = H.fx[i].x + w / 2, oy = H.fx[i].y + mt + h / 2;
    /* point u (element space, origin at the box's top left) -> o + sc * (R (u - o') - (w/2, h/2)) with o' = (w/2, h/2) */
    float m[6];
    m[0] = sc * c; m[1] = sc * s; m[2] = -sc * s; m[3] = sc * c;
    float ex = -8 - w / 2, ey = -8 - h / 2;   /* the canvas's pixel (0,0) relative to the box centre */
    m[4] = ox + sc * (c * ex - s * ey - w / 2);
    m[5] = oy + sc * (s * ex + c * ey - h / 2);
    ov_draw_m(H.fx[i].L.t, m, op, 1);
  }
}

void ui_draw(void) {
  ov_begin();
  ov_vignette();
  if (!ui_test_no_grain) ov_grain(ui_now);
  if (H.on) draw_hud();
  scr_draw();
  /* #flash: opacity P -> 0 over e ms, ease-out */
  if (H.flash_dur > 0) {
    float t = (ui_now - H.flash_t0) / H.flash_dur;
    if (t < 1) ov_fill(0, 0, CK_W, CK_H, H.flash_rgb, H.flash_a0 * (1 - EASE_OUT(t)));
  }
  ov_end();
}

/* ================================================================== screens API */

const char *ui_screen(void) { return scr_cur; }
void ui_show(const char *s) { scr_show(s); }
void ui_hide_all(void) { scr_show(NULL); }
void ui_key(int code) { if (scr_cur && strcmp(scr_cur, "s-load")) scr_key(code); }
void ui_pointer(float x, float y, bool pressed) { if (scr_cur) scr_pointer(x, y, pressed); }
void ui_set_select(ck_sel *sel, int mode) { scr_sel = sel; scr_mode = mode; scr_sel_focus = mode == 1 ? 3 : 2; scr_select_changed(); }
void ui_render_select(void) { scr_select_changed(); }
void ui_set_end(const char *name, bool wins, const char *sub) { scr_end_changed(name, wins, sub); }
void ui_load(float p, const char *msg) { scr_load(p, msg, false); }
void ui_load_error(const char *msg) { scr_load(-1, msg, true); }

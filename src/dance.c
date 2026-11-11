/* dance.c: see dance.h. The page's sv class. Its canvas is redrawn every
 * frame in the page; here the same drawing is split so the cart is not
 * rasterising 1280 x 720 each frame: the static lanes are painted once per
 * dance, the arrows are painted once as sprites (fill, stroke and white
 * stroke apart, so the receptor flash composes exactly as the canvas draws
 * it) and placed through the overlay's GPU transforms, and the texts are
 * repainted only when they change. */
#include "dance.h"
#include "songs.h"
#include "audio.h"
#include "input.h"
#include "overlay.h"
#include "cv.h"
#include "ck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void (*dance_on_move)(int player, int lane, int judgement);

static const char *LANE_C[4] = {"#ef6ad6", "#57bdf5", "#86ee72", "#ff9d5c"};       /* $w */
static const char *LANE_D[4] = {"#b31d8f", "#1e3fa8", "#1f9a34", "#c2551a"};       /* e3 */
static const float ROT[4] = {-CK_PI / 2, CK_PI, 0, CK_PI / 2};                       /* s3 */
static const char *Z8 = "M 256 22 L 484 248 L 484 305 L 438 345 L 373 345 L 337 306 L 337 412 L 256 481 L 175 412 L 175 306 L 139 345 L 74 345 L 28 305 L 28 248 Z";
static const char *T3[3] = {"M 256 99 L 415 248 L 417 275 L 392 277 L 300 205 L 256 169 L 212 205 L 120 277 L 95 275 L 97 248 Z",
  "M 256 185 L 286 213 L 284 289 L 262 272 L 256 272 L 250 272 L 228 289 L 226 213 Z",
  "M 256 289 L 285 309 L 284 382 L 256 404 L 228 382 L 227 309 Z"};
static const struct { const char *n; int p; const char *c; } JUDGE[4] = {{"PERFECT", 100, "#f2ab1e"}, {"GREAT", 70, "#ece3d3"}, {"OK", 40, "#8fbde6"}, {"MISS", 0, "#d24f3a"}};   /* yC */
#define R8 1.45f
#define J0 0.06f
#define J1 0.115f
#define TV 0.18f
#define MAXN 400

typedef struct {
  dance_player d;
  int score, combo, max_combo, counts[4];
  int judged[MAXN];
  bool prev[4];          /* l d u r of the stick last frame */
  bool final, sent;
  struct { int i; bool hit; float t, off; } plan[MAXN];
  float flash[4];
  int pop_j; double pop_t;
} player;

static struct {
  bool active, done, has_result, result_taken;
  uint32_t seed;
  const song_t *track;
  float xe;
  chart_note chart[MAXN]; int nc;
  player p[2]; int np;
  void (*net)(mp_val *);
  bool use_audio; double t0, wall0;
  double finish_at, result_at;
  dance_result result;
  float beat;
  /* drawing */
  cv *lanes_cv; ov_tex *lanes;
  ov_tex *spr_note[4], *spr_rec_fill_off, *spr_rec_stroke_off, *spr_rec_fill[4], *spr_rec_stroke[4], *spr_rec_white;
  cv *txt_cv; ov_tex *txt[16]; char txt_key[16][48];
  cv *ring_cv; ov_tex *ring[8];
} D;

#define SPR 128   /* sprite size: the arrows draw at about 90 to 106 px */

static double now_ms(void) { return ck_now() * 1000; }
static uint32_t mix_rng_state;
static float d3(void) {   /* D3: the CPU plan's seeded generator */
  mix_rng_state = mix_rng_state + 1831565813u;
  uint32_t P = mix_rng_state;
  P = (uint32_t)((uint64_t)(P ^ (P >> 15)) * (P | 1));
  P ^= P + (uint32_t)((uint64_t)(P ^ (P >> 7)) * (P | 61));
  return (float)((P ^ (P >> 14)) / 4294967296.0);
}

/* ---- sprites ---- */
static ov_tex *sprite(void (*paint)(cv *, int), int lane) {
  cv *c = cv_new(SPR, SPR);
  cv_scale(c, SPR / 512.f, SPR / 512.f);
  cv_line_join(c, CV_JOIN_ROUND); cv_line_cap(c, CV_CAP_ROUND);
  paint(c, lane);
  ov_tex *t = ov_tex_new(SPR, SPR);
  ov_tex_from_cv(t, c);
  cv_free(c);
  return t;
}
static void paint_note(cv *c, int L) {
  cv_fill_linear(c, 0, 22, 0, 481); cv_fill_stop(c, 0, LANE_D[L]); cv_fill_stop(c, 1, LANE_C[L]);
  cv_begin(c); cv_svg_path(c, Z8); cv_fill(c);
  cv_line_width(c, 25); cv_stroke_css(c, "#0c0c14"); cv_stroke(c);
  cv_fill_css(c, "#fff");
  for (int i = 0; i < 3; i++) { cv_begin(c); cv_svg_path(c, T3[i]); cv_fill(c); }
}
static void paint_fill_off(cv *c, int L) { (void)L; cv_fill_css(c, "rgba(236,227,211,0.07)"); cv_begin(c); cv_svg_path(c, Z8); cv_fill(c); }
static void paint_stroke_off(cv *c, int L) { (void)L; cv_line_width(c, 25); cv_stroke_css(c, "rgba(236,227,211,0.6)"); cv_begin(c); cv_svg_path(c, Z8); cv_stroke(c); }
static void paint_fill_on(cv *c, int L) { cv_fill_css(c, LANE_C[L]); cv_begin(c); cv_svg_path(c, Z8); cv_fill(c); }
static void paint_stroke_on(cv *c, int L) { cv_line_width(c, 25); cv_stroke_css(c, LANE_C[L]); cv_begin(c); cv_svg_path(c, Z8); cv_stroke(c); }
static void paint_white(cv *c, int L) { (void)L; cv_line_width(c, 18); cv_stroke_css(c, "#fff"); cv_begin(c); cv_svg_path(c, Z8); cv_stroke(c); }

void dance_init(void) {
  for (int L = 0; L < 4; L++) {
    D.spr_note[L] = sprite(paint_note, L);
    D.spr_rec_fill[L] = sprite(paint_fill_on, L);
    D.spr_rec_stroke[L] = sprite(paint_stroke_on, L);
  }
  D.spr_rec_fill_off = sprite(paint_fill_off, 0);
  D.spr_rec_stroke_off = sprite(paint_stroke_off, 0);
  D.spr_rec_white = sprite(paint_white, 0);
  D.lanes_cv = cv_new(CK_W, CK_H); D.lanes = ov_tex_new(CK_W, CK_H);
  D.txt_cv = cv_new(1280, 220);
  for (int i = 0; i < 16; i++) D.txt[i] = ov_tex_new(1280, 220);
  D.ring_cv = cv_new(256, 256);
  for (int i = 0; i < 8; i++) D.ring[i] = ov_tex_new(256, 256);
}

/* ---- the chart and the players ---- */

static void plan_cpu(player *p) {
  mix_rng_state = D.seed ^ 2654435769u;
  for (int i = 0; i < D.nc; i++) {
    bool w = d3() < p->d.acc;
    float g = (d3() - 0.5f) * 2 * (0.02f + (1 - p->d.acc) * 0.12f);
    p->plan[i].i = i; p->plan[i].hit = w; p->plan[i].t = D.chart[i].beat * D.xe + g; p->plan[i].off = g;
  }
}

static double song_time(void) {
  return D.use_audio ? audio_time() - audio_latency() - D.t0 : ck_now() - D.wall0;
}

static void paint_lanes(void);

void dance_start(uint32_t seed, const dance_player *players, int n, void (*net)(mp_val *)) {
  D.seed = seed;
  D.track = song_from_seed(seed);
  D.xe = 60.f / D.track->bpm;
  D.nc = song_chart(D.track, D.chart, MAXN);
  D.np = n;
  for (int i = 0; i < n; i++) {
    player *p = &D.p[i];
    memset(p, 0, sizeof *p);
    p->d = players[i];
    for (int k = 0; k < D.nc; k++) p->judged[k] = -1;
    p->pop_j = -1;
    if (p->d.kind == DP_CPU) plan_cpu(p);
  }
  D.net = net; D.done = false; D.has_result = false; D.result_taken = false;
  D.active = true;
  for (int i = 0; i < 16; i++) D.txt_key[i][0] = 0;
  paint_lanes();
  /* await tt.dance(true, track): the first step's audio time, or the wall clock + .5 */
  int idx = 0;
  for (int k = 0; k < song_count(); k++) if (song_get(k) == D.track) idx = k;
  double w = music_dance_start(idx);
  D.use_audio = w >= 0;
  if (D.use_audio) D.t0 = w; else D.wall0 = ck_now() + 0.5;
}

static void judge(int A, int P, int e, bool remote) {
  player *d = &D.p[A];
  if (d->judged[P] >= 0) return;
  d->judged[P] = e; d->counts[e]++;
  if (e < 3) {
    d->combo++; if (d->combo > d->max_combo) d->max_combo = d->combo;
    d->score += (int)lround(JUDGE[e].p * (1 + fminf((float)d->combo, 40) * 0.025f));
  } else d->combo = 0;
  d->pop_j = e; d->pop_t = now_ms();
  if (dance_on_move) dance_on_move(A, D.chart[P].lane, e);
  if (!remote && d->d.kind == DP_HUMAN && D.net) {
    mp_val *m = mp_map(); mp_set(m, "k", mp_str("dh")); mp_set(m, "i", mp_num(P)); mp_set(m, "j", mp_num(e)); mp_set(m, "sc", mp_num(d->score));
    D.net(m);
  }
}

static void press(int A, int P) {
  if (A < 0 || !D.active || D.done) return;
  player *e = &D.p[A];
  if (e->d.kind != DP_HUMAN) return;
  double t = song_time();
  int Dn = -1; double w = 1e9;
  for (int g = 0; g < D.nc; g++) {
    if (e->judged[g] >= 0 || D.chart[g].lane != P) continue;
    double i = fabs(D.chart[g].beat * D.xe - t);
    if (i < w) { w = i; Dn = g; }
    if (D.chart[g].beat * D.xe > t + TV + 0.1) break;
  }
  e->flash[P] = 1;
  if (Dn >= 0 && w <= TV) judge(A, Dn, w <= J0 ? 0 : w <= J1 ? 1 : 2, false);
}

void dance_remote_hit(int A, int P, int e) {
  for (int t = 0; t < D.np; t++) if (D.p[t].d.kind == DP_REMOTE) {
    if (A < 0 || A >= D.nc || D.p[t].judged[A] >= 0) return;
    judge(t, A, P, true); D.p[t].score = e; return;
  }
}
void dance_remote_final(int A, int P) {
  for (int t = 0; t < D.np; t++) if (D.p[t].d.kind == DP_REMOTE) { D.p[t].score = A; D.p[t].max_combo = P; D.p[t].final = true; }
}

static dance_result compute_result(void) {
  dance_result r; memset(&r, 0, sizeof r);
  if (D.np == 1) {
    player *t = &D.p[0];
    float w = (t->counts[0] * 100 + t->counts[1] * 70 + t->counts[2] * 40) / (float)(D.nc * 100);
    const char *g; int m;
    if (w >= 0.9f) { g = "S"; m = 80; } else if (w >= 0.75f) { g = "A"; m = 60; } else if (w >= 0.55f) { g = "B"; m = 40; }
    else if (w >= 0.35f) { g = "C"; m = 20; } else { g = "D"; m = 0; }
    r.solo = true; r.winner = 0; snprintf(r.grade, 2, "%s", g); r.meter = m; r.acc = w;
    r.scores[0] = t->score; r.combos[0] = t->max_combo;
    return r;
  }
  r.winner = D.p[0].score > D.p[1].score ? 0 : D.p[1].score > D.p[0].score ? 1 : -1;
  r.scores[0] = D.p[0].score; r.scores[1] = D.p[1].score; r.combos[0] = D.p[0].max_combo; r.combos[1] = D.p[1].max_combo;
  return r;
}

static void stop(void) { D.active = false; music_dance_stop(); }
void dance_cancel(void) { if (D.active) stop(); }
bool dance_active(void) { return D.active; }
float dance_beat_pulse(void) { return D.beat; }
bool dance_take_result(dance_result *out) {
  if (!D.has_result || D.result_taken || D.active) return false;
  D.result_taken = true; *out = D.result;
  return true;
}

void dance_update(float A) {
  if (!D.active) return;
  double e = song_time();
  /* pollInputs: every human's stick as four lanes, on the edge */
  for (int P = 0; P < D.np; P++) {
    player *p = &D.p[P];
    if (p->d.kind != DP_HUMAN) continue;
    intent_t t = input_poll(p->d.input_slot);
    bool l = t.mx < -0.55f, r = t.mx > 0.55f, u = t.my > 0.55f, d = t.my < -0.55f;
    if (l && !p->prev[0]) press(P, 0);
    if (d && !p->prev[1]) press(P, 1);
    if (u && !p->prev[2]) press(P, 2);
    if (r && !p->prev[3]) press(P, 3);
    p->prev[0] = l; p->prev[1] = d; p->prev[2] = u; p->prev[3] = r;
  }
  for (int k = 0; k < D.np; k++) {
    player *Dp = &D.p[k];
    if (Dp->d.kind == DP_CPU) {
      for (int w = 0; w < D.nc; w++) {
        if (Dp->judged[Dp->plan[w].i] >= 0 || e < Dp->plan[w].t) continue;
        if (Dp->plan[w].hit) { float g = fabsf(Dp->plan[w].off); judge(k, Dp->plan[w].i, g <= J0 ? 0 : g <= J1 ? 1 : 2, false); }
        else if (e > Dp->plan[w].t + TV) judge(k, Dp->plan[w].i, 3, false);
      }
    }
    for (int w = 0; w < D.nc; w++) {
      if (Dp->judged[w] >= 0 || Dp->d.kind == DP_REMOTE) continue;
      if (e > D.chart[w].beat * D.xe + TV) judge(k, w, 3, true);
      else break;
    }
    for (int w = 0; w < 4; w++) Dp->flash[w] = fmaxf(0, Dp->flash[w] - A * 5);
  }
  double t = (8 + D.track->song_beats + 2) * D.xe;
  if (e > t && !D.done) {
    D.done = true;
    for (int k = 0; k < D.np; k++) if (D.p[k].d.kind == DP_HUMAN) {
      if (D.net) { mp_val *m = mp_map(); mp_set(m, "k", mp_str("df")); mp_set(m, "sc", mp_num(D.p[k].score)); mp_set(m, "mc", mp_num(D.p[k].max_combo)); D.net(m); }
      break;
    }
    for (int k = 0; k < D.np; k++) if (D.p[k].d.kind != DP_REMOTE) D.p[k].final = true;
    D.finish_at = now_ms();
  }
  if (D.done) {
    player *rem = NULL;
    for (int k = 0; k < D.np; k++) if (D.p[k].d.kind == DP_REMOTE) rem = &D.p[k];
    if ((!rem || rem->final || now_ms() - D.finish_at > 3500) && !D.has_result) {
      D.result = compute_result(); D.has_result = true; D.result_at = now_ms(); sfx_win();
    }
    if (D.has_result && now_ms() - D.result_at > 2800) stop();
  }
  D.beat = fmaxf(0, 1 - (float)fmod(e / D.xe, 1.0) * 2.2f);
}

/* ---- drawing ---- */

static float G_, N_, R_, V_, BX[2];
static float o_(float a) { return (a - N_ + V_) / V_; }
static float j_(int pl, float T, float E) { return BX[pl] + (T - 1.5f) * G_ * o_(E); }

static void geometry(void) {
  float P = CK_W, e = CK_H;
  int w = D.np;
  G_ = fminf(P * (w == 2 ? 0.062f : 0.075f), e * 0.13f);
  N_ = e * 0.2f; R_ = e * 0.74f; V_ = e * 2.2f;
  if (w == 2) { BX[0] = P * 0.17f; BX[1] = P * 0.83f; } else BX[0] = BX[1] = P * 0.5f;
}

static void paint_lanes(void) {
  geometry();
  cv *A = D.lanes_cv;
  cv_clear(A);
  float e = CK_H, f = e * 0.98f, u = 0.55f * G_;
  for (int c = 0; c < D.np; c++) {
    float Q = BX[c];
    cv_begin(A);
    cv_move(A, j_(c, 0, N_) - u, N_ - G_ * 0.6f); cv_line(A, j_(c, 3, N_) + u, N_ - G_ * 0.6f);
    cv_line(A, j_(c, 3, f) + u * o_(f), f); cv_line(A, j_(c, 0, f) - u * o_(f), f); cv_close(A);
    cv_fill_css(A, "rgba(21,18,15,0.66)"); cv_fill(A);
    cv_stroke_css(A, "#ece3d3"); cv_line_width(A, 3); cv_stroke(A);
    cv_line_width(A, 1.5f); cv_stroke_css(A, "rgba(236,227,211,0.16)");
    for (int T = 0; T <= 4; T++) {
      float E = (T - 2) * G_;
      cv_begin(A); cv_move(A, Q + E, N_); cv_line(A, Q + E * o_(f), f); cv_stroke(A);
    }
  }
  ov_tex_from_cv(D.lanes, A);
}

/* a text layer: repainted only when its key changes */
static void text_layer(int slot, const char *key, void (*paint)(cv *, void *), void *ctx, float alpha, float dx, float dy) {
  if (strcmp(D.txt_key[slot], key)) {
    snprintf(D.txt_key[slot], sizeof D.txt_key[slot], "%s", key);
    cv_clear(D.txt_cv);
    paint(D.txt_cv, ctx);
    ov_tex_from_cv(D.txt[slot], D.txt_cv);
  }
  float m[6] = {1, 0, 0, 1, dx, dy};
  ov_draw_m(D.txt[slot], m, alpha, 1);
}

typedef struct { const char *s; int font; float px; const char *fill; const char *shadow; float sx, sy; float cx, cy; float stroke_w; const char *stroke; } tx;
static void paint_tx(cv *c, void *p) {
  tx *t = p;
  cv_text_align(c, CV_CENTER); cv_text_baseline(c, CV_MIDDLE);
  cv_font(c, t->font, t->px);
  if (t->shadow) cv_shadow(c, t->shadow, 0, t->sx, t->sy);
  if (t->stroke) { cv_line_width(c, t->stroke_w); cv_stroke_css(c, t->stroke); cv_line_join(c, CV_JOIN_MITER); cv_stroke_text(c, t->s, t->cx, t->cy); }
  cv_fill_css(c, t->fill); cv_fill_text(c, t->s, t->cx, t->cy);
  cv_no_shadow(c);
}
/* draw text centred at (x, y) through the 1280 x 220 text canvas (its centre row is y 110) */
static void text_at(int slot, const char *s, int font, float px, const char *fill, float x, float y, float alpha, const char *shadow, float sh) {
  char key[48]; snprintf(key, sizeof key, "%s|%d|%.1f|%s", s, font, px, fill);
  tx t = {s, font, px, fill, shadow, sh, sh, 640, 110, 0, NULL};
  text_layer(slot, key, paint_tx, &t, alpha, x - 640, y - 110);
}

static void draw_arrow(float x, float y, float size, int L, bool rec, float g) {
  float n = size / SPR * (rec ? 1 + 0.14f * g : 1);
  float m[6];
  ov_matrix(m, x - SPR / 2.f, y - SPR / 2.f, SPR / 2.f, SPR / 2.f, ROT[L], n, n, 0, 0);
  if (!rec) { ov_draw_m(D.spr_note[L], m, 1, 1); return; }
  if (g > 0) {
    ov_draw_m(D.spr_rec_fill[L], m, 0.25f + 0.6f * g, 1);
    ov_draw_m(D.spr_rec_stroke[L], m, 1, 1);
    ov_draw_m(D.spr_rec_white, m, g, 1);
  } else {
    ov_draw_m(D.spr_rec_fill_off, m, 1, 1);
    ov_draw_m(D.spr_rec_stroke_off, m, 1, 1);
  }
}
static void draw_note(float x, float y, float size, int L, float alpha) {
  float n = size / SPR, m[6];
  ov_matrix(m, x - SPR / 2.f, y - SPR / 2.f, SPR / 2.f, SPR / 2.f, ROT[L], n, n, 0, 0);
  ov_draw_m(D.spr_note[L], m, alpha, 1);
}
static void draw_ring(int k, float x, float y, float r, int L, float alpha) {
  cv *c = D.ring_cv;
  cv_clear(c);
  cv_stroke_css(c, LANE_C[L]); cv_line_width(c, 4);
  cv_begin(c); cv_arc(c, 128, 128, fminf(r, 124), 0, CK_PI * 2, false); cv_stroke(c);
  ov_tex_from_cv(D.ring[k], c);
  ov_draw(D.ring[k], x - 128, y - 128, alpha);
}

void dance_draw(void) {
  if (!D.active) return;
  geometry();
  float P = CK_W, e = CK_H, t = 1;
  double Dt = song_time();
  float xe = D.xe;
  ov_begin();
  text_at(0, D.np == 1 ? "DANCE BREAK" : "DANCE BATTLE", F_MARKER, roundf(e * 0.055f), "#f2ab1e", P / 2, e * 0.075f, 1, "#c2542c", 5 * t);
  text_at(1, D.track ? (Dt < 0 ? "GET READY" : D.track->name) : "", F_TYPE, roundf(e * 0.022f), "#a79d8c", P / 2, e * 0.125f, 1, NULL, 0);
  if (Dt < 9 * xe) {
    float a = (float)(Dt / xe);
    const char *c = a < 2 ? "GET READY" : a < 4 ? "3" : a < 6 ? "2" : a < 8 ? "1" : "GO";
    float Q = a < 2 ? a / 2 : fmodf(a, 2) / 2;
    float m = e * (strlen(c) > 2 ? 0.1f : 0.22f) * (1 + 0.25f * fmaxf(0, 1 - Q * 3));
    float al = a >= 8 ? fmaxf(0, 1 - (a - 8) * 1.1f) : 1;
    char key[48]; snprintf(key, sizeof key, "cd|%s|%d", c, (int)roundf(m));
    tx T = {c, F_MARKER, roundf(m), !strcmp(c, "GO") ? "#5fb892" : "#f2ab1e", NULL, 0, 0, 640, 110, roundf(e * 0.012f), "#15120f"};
    text_layer(2, key, paint_tx, &T, al, P / 2 - 640, e * 0.42f - 110);
  }
  float z = (float)fmax(0, fmin(1, Dt / ((8 + D.track->song_beats) * xe)));
  ov_fill(P * 0.3f, e * 0.15f, P * 0.4f, 6 * t, C_CREAM, 0.18f);
  ov_fill(P * 0.3f, e * 0.15f, P * 0.4f * z, 6 * t, C_PINK, 1);
  ov_draw(D.lanes, 0, 0, 1);
  float f = e * 0.98f;
  for (int c = 0; c < D.np; c++) {
    player *a = &D.p[c];
    float Q = BX[c];
    /* the beat lines */
    int b0 = (int)ceil(Dt / xe);
    for (int T = b0; T < b0 + (int)ceilf(R8 / xe) + 1; T++) {
      float E = N_ + (float)((T * xe - Dt) / R8) * R_;
      if (E < N_ || E > f) continue;
      float hw = 2 * G_ * o_(E);
      ov_fill(Q - hw, E - 0.75f, hw * 2, 1.5f, C_CREAM, 0.07f);
    }
    text_at(3 + c * 4, a->d.name, F_MARKER, roundf(e * 0.034f), "#ece3d3", Q, e * 0.2f - G_ * 0.8f, 1, NULL, 0);
    char sc[16]; snprintf(sc, sizeof sc, "%05d", a->score);
    text_at(4 + c * 4, sc, F_LCD, roundf(e * 0.03f), "#f2ab1e", Q, e * 0.2f - G_ * 0.8f + e * 0.04f, 1, NULL, 0);
    for (int T = 0; T < 4; T++) {
      draw_arrow(j_(c, (float)T, N_), N_, G_ * 0.95f, T, true, a->flash[T]);
      float g = a->flash[T];
      if (g > 0) draw_ring(c * 4 + T, j_(c, (float)T, N_), N_, G_ * 0.95f * (0.55f + (1 - g) * 0.7f), T, g * 0.8f);
    }
    for (int T = 0; T < D.nc; T++) {
      chart_note *E = &D.chart[T];
      float h = (float)(E->beat * xe - Dt);
      if (h > R8) break;
      if (h < -0.25f) continue;
      float d = N_ + fmaxf(0, h) / R8 * R_ + (h < 0 ? h / R8 * R_ : 0);
      int b = a->judged[T];
      if (b >= 0 && b < 3) continue;
      draw_note(j_(c, (float)E->lane, d), d, G_ * 0.95f * (1 + (o_(d) - 1) * 0.5f), E->lane, b == 3 ? 0.3f : 1);
    }
    if (a->pop_j >= 0 && now_ms() - a->pop_t < 650) {
      float T = (float)(1 - (now_ms() - a->pop_t) / 650);
      text_at(5 + c * 4, JUDGE[a->pop_j].n, F_MARKER, roundf(e * 0.05f), JUDGE[a->pop_j].c, Q, e * 0.52f - (1 - T) * 20 * t, T, NULL, 0);
    }
    if (a->combo >= 3) {
      char cb[24]; snprintf(cb, sizeof cb, "%d COMBO", a->combo);
      text_at(6 + c * 4, cb, F_MARKER, roundf(e * 0.035f), "#ece3d3", Q, e * 0.58f, 1, NULL, 0);
    }
  }
  if (D.has_result) {
    ov_fill(0, e * 0.3f, P, e * 0.32f, C_INK, 0.8f);
    dance_result *r = &D.result;
    char big[40], small[64];
    if (r->solo) {
      snprintf(big, sizeof big, "GRADE %s", r->grade);
      if (r->meter > 0) snprintf(small, sizeof small, "EXTREME METER +%d FOR THE NEXT ROUND", r->meter);
      else snprintf(small, sizeof small, "NO METER THIS TIME");
    } else {
      if (r->winner >= 0) snprintf(big, sizeof big, "%s WINS", D.p[r->winner].d.name); else snprintf(big, sizeof big, "DEAD HEAT");
      snprintf(small, sizeof small, r->winner >= 0 ? "EXTREME METER CHARGED FOR THE NEXT ROUND" : "NO ONE GETS A BONUS");
    }
    text_at(11, big, F_MARKER, roundf(e * 0.09f), "#f2ab1e", P / 2, e * 0.43f, 1, NULL, 0);
    text_at(12, small, F_TYPE, roundf(e * 0.03f), "#ece3d3", P / 2, e * 0.52f, 1, NULL, 0);
  }
  ov_end();
}

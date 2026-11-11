/* ui_menus.c: the screens this cart adds, in the page's style (ui_kit.h): the fighter select
 * (the page's #s-select, grown to the whole roster and rack), the stage select, and the
 * tourney's setup, bracket and champion screens. Each paints into one canvas when what it
 * shows changes, and keeps hit rects for the mouse; the keyboard and the pads move a focus
 * row by row (up and down), change the focused row (left and right), and act (Enter).
 *
 * What they show comes from the tables (roster.h, weapons.h, stage.h) and the tourney
 * (tourney.h); what they change goes back through ui_cb, except the tourney's setup, which
 * is plain data the screen edits with tourney.c's own calls. */
#include "ui_int.h"
#include "ui_kit.h"
#include "ui_hint.h"
#include "ck.h"
#include "audio.h"
#include "input.h"
#include "roster.h"
#include "weapons.h"
#include "stage.h"
#include "special.h"
#include "tourney.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static tourney_t *TT;
void ui_set_tourney(void *t) { TT = (tourney_t *)t; }

static void act(const char *a) { sfx_ui_ok(); if (ui_cb.act) ui_cb.act(a); }
static uint32_t slot_col(int g) { return g ? C_CYAN : C_PINK; }

/* a big head over a screen: marker 51.2, ls .02em, shadows 5 5 pink and 2px ink (the page's .head) */
static void head_paint(layer *L, const char *text) {
  static const tshadow SH = {5, {{5, 5, COL(C_PINK)}, {-2, -2, COL(C_INK)}, {2, -2, COL(C_INK)}, {-2, 2, COL(C_INK)}, {2, 2, COL(C_INK)}}};
  L_ensure(L, 900, 80);
  cv *c = L->c;
  cv_set_transform(c, 1, 0, 0, 1, -190, 0);
  float hw = t_width(F_MARKER, 51.2f, 1.024f, text);
  t_draw_sh(c, F_MARKER, 51.2f, 1.024f, 640 - hw / 2, 18 + t_asc(F_MARKER, 51.2f), text, COL(C_CREAM), &SH);
}
static layer HD_SEL, HD_STAGE, HD_TOURNEY;
static void head_draw(layer *L) { L_upload(L); ov_draw(L->t, 190, 0, 1); }

/* a marker line shrunk to fit a width */
static float fit_fs(const char *s, float fs, float w, float min) {
  while (fs > min && t_width(F_MARKER, fs, 0, s) > w) fs -= 1;
  return fs;
}
/* a small arrow: left (-1) or right (+1), centred on (x, y) */
static void arrow(cv *c, float x, float y, int dir, col_t col) {
  cv_begin(c);
  cv_move(c, x - 5 * dir, y - 8); cv_line(c, x + 6 * dir, y); cv_line(c, x - 5 * dir, y + 8);
  cv_close(c);
  cv_fill_hex(c, col.rgb, col.a); cv_fill(c);
}

/* ================================================================== the stage select */
/* A strip of every stage (and RANDOM) along the bottom: a still of each, baked from the game by
 * tools/bake-stills.mjs, and its short name. The stage itself stands behind the screen once it is
 * loaded; a KEYHOLDER stage that is not loaded yet shows its still large instead. A (Enter) or a
 * second click fights on the stage picked; B (Escape) goes back to the fighters. */
#define ST_CELLS (STAGE_N + 1)            /* the stages, then RANDOM */
#define ST_TW 110
#define ST_TH 62
#define ST_GAP 8
#define ST_Y 590
static const char *SHORT[] = {"WORKSHOP", "THE LAB", "MAIN ST", "BACK LOT", "LONG BENCH", "LASER CORNER", "THE FRIDGE", "MACHINE SHOP", "UNFOLDED"};
static struct { uint8_t *px; } THUMB[16];
static layer st_card, st_strip, st_prev; static int st_prev_of = -2;
static hint_line st_hint;
static float st_x(int k) { return (CK_W - (ST_CELLS * ST_TW + (ST_CELLS - 1) * ST_GAP)) / 2.f + k * (ST_TW + ST_GAP); }
static int st_cell(void) { return !scr_sel ? 0 : scr_sel->stage < 0 ? STAGE_N : scr_sel->stage; }
/* a still, decoded (and averaged down to w x h, when given) */
static uint8_t *still_load(int s, int *w, int *h, int tw, int th) {
  char n[48]; snprintf(n, sizeof n, "still_%s.jpg", STAGES[s].key);
  size_t len; uint8_t *d = ck_asset(n, &len);
  if (!d) return NULL;
  t3_texture *T = t3_texture_load_memory(d, len);
  free(d);
  if (!T || !T->pixels) { if (T) t3_release(T); return NULL; }
  int W = T->width, H = T->height, ow = tw ? tw : W, oh = th ? th : H;
  uint8_t *o = malloc((size_t)ow * oh * 4);
  for (int y = 0; y < oh; y++) for (int x = 0; x < ow; x++) {
    int x0 = x * W / ow, x1 = (x + 1) * W / ow, y0 = y * H / oh, y1 = (y + 1) * H / oh, n2 = 0;
    unsigned acc[3] = {0, 0, 0};
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { const uint8_t *p = T->pixels + ((size_t)yy * W + xx) * 4; acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; n2++; }
    uint8_t *q = o + ((size_t)y * ow + x) * 4;
    for (int c = 0; c < 3; c++) q[c] = (uint8_t)(acc[c] / (n2 ? n2 : 1));
    q[3] = 255;
  }
  t3_release(T);
  *w = ow; *h = oh;
  return o;
}
static void stills_init(void) {
  for (int s = 0; s < STAGE_N && s < 16; s++) { int w, h; THUMB[s].px = still_load(s, &w, &h, ST_TW - 6, ST_TH - 6); }
}
static void stage_paint(void) {
  nhits = 0;
  if (!scr_sel) return;
  int cell = st_cell();
  bool rnd = cell == STAGE_N;
  const stage_def *d = rnd ? NULL : &STAGES[cell];
  /* the card: the name, the line, where it comes from; FIGHT */
  L_ensure(&st_card, 1180, 120);
  cv *c = st_card.c;
  c_box(c, 0, 0, 1160, 104, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(C_PINK));
  const char *nm = rnd ? "RANDOM" : d->name;
  float fs = fit_fs(nm, 40, 760, 26);
  t_draw(c, F_MARKER, fs, 0, 24, 18 + fs * 0.74f, nm, COL(C_INK));
  const char *bl = rnd ? "A different stage every time you press it." : d->blurb;
  t_draw(c, F_TYPE, 15, 0, 24, 76, bl, COLA(C_INK, 0.9f));
  const char *note = rnd ? "" : d->web ? "THE WEB VERSION'S OWN SHOP" : stage_needs_load(cell) ? "FROM KEYHOLDER. LOADS WHEN THE FIGHT STARTS" : "FROM KEYHOLDER";
  t_draw(c, F_TYPE, 14, 1.4f, 24, 96, note, COLA(C_INK, 0.7f));
  btn_t go; btn_init(&go, 1160 - 24 - 280, 20, 280, 64, 38.4f, 22, 8, BTN_GO, "FIGHT!", "start");
  btn_paint_at(c, &go, 1, 0, 0);
  hit_add(60 + go.x, 470 + go.y, go.w, go.h, H_ACT, 0, 0, "start");
  /* the strip */
  float sw = st_x(ST_CELLS - 1) + ST_TW - st_x(0);
  L_ensure(&st_strip, (int)sw + 20, ST_TH + 44);
  c = st_strip.c;
  for (int k = 0; k < ST_CELLS; k++) {
    float x = st_x(k) - st_x(0) + 6, y = 6;
    bool on = k == cell;
    if (on) { x -= 3; y -= 3; }
    c_rect(c, x + (on ? 7 : 4), y + (on ? 7 : 4), ST_TW, ST_TH, COL(C_INK));
    c_rect(c, x, y, ST_TW, ST_TH, COL(on ? C_PINK : C_PAPER));
    if (k < STAGE_N && THUMB[k].px) cv_draw_image(c, THUMB[k].px, ST_TW - 6, ST_TH - 6, x + 3, y + 3, ST_TW - 6, ST_TH - 6);
    else if (k == STAGE_N) {
      static const tshadow QS = {4, {{-2, -2, COL(C_INK)}, {2, -2, COL(C_INK)}, {-2, 2, COL(C_INK)}, {2, 2, COL(C_INK)}}};
      for (float q = -ST_TH; q < ST_TW; q += 16) {
        cv_begin(c); cv_move(c, x + 3 + q, y + ST_TH - 3); cv_line(c, x + 3 + q + 7, y + ST_TH - 3); cv_line(c, x + 3 + q + 7 + ST_TH - 6, y + 3); cv_line(c, x + 3 + q + ST_TH - 6, y + 3); cv_close(c);
        cv_save(c); cv_begin(c); cv_rect(c, x + 3, y + 3, ST_TW - 6, ST_TH - 6); cv_clip(c);
        cv_begin(c); cv_move(c, x + 3 + q, y + ST_TH - 3); cv_line(c, x + 3 + q + 7, y + ST_TH - 3); cv_line(c, x + 3 + q + 7 + ST_TH - 6, y + 3); cv_line(c, x + 3 + q + ST_TH - 6, y + 3); cv_close(c);
        cv_fill_hex(c, C_PINK, 1); cv_fill(c); cv_restore(c);
      }
      float tw = t_width(F_MARKER, 34, 0, "?");
      t_draw_sh(c, F_MARKER, 34, 0, x + (ST_TW - tw) / 2, y + ST_TH / 2 + 12, "?", COL(C_CREAM), &QS);
    }
    c_border(c, x, y, ST_TW, ST_TH, 3, COL(C_INK));
    const char *sn = k < STAGE_N ? SHORT[k] : "RANDOM";
    float nfs = 14; while (nfs > 12 && t_width(F_TYPE, nfs, 0, sn) > ST_TW) nfs -= 0.5f;
    float nw = t_width(F_TYPE, nfs, 0, sn);
    static const tshadow NS = {1, {{1, 1, COL(C_INK)}}};
    t_draw_sh(c, F_TYPE, nfs, 0, x + (ST_TW - nw) / 2, y + ST_TH + 22, sn, on ? COL(C_PINK) : COL(C_CREAM), &NS);
    hit_add(st_x(k), ST_Y, ST_TW, ST_TH + 26, H_STAGE, 100 + k, 0, NULL);
  }
  /* the preview of a stage that is not loaded yet: its still, large */
  if (!rnd && stage_needs_load(cell) && st_prev_of != cell) {
    int w, h; uint8_t *px = still_load(cell, &w, &h, 0, 0);
    st_prev_of = cell;
    if (px) {
      L_ensure(&st_prev, w + 24, h + 24);
      c_box(st_prev.c, 0, 0, w + 8, h + 8, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(C_PINK));
      cv_draw_image(st_prev.c, px, w, h, 4, 4, w, h);
      free(px);
    } else st_prev_of = -1;
  }
}
static void stage_pick(int k) {
  if (k == STAGE_N) { if (scr_sel) scr_sel->stage = -1; ui_menus_changed(); }
  else if (ui_cb.stage) ui_cb.stage(k);
  sfx_ui();
}
static void stage_key(int e) {
  int k = st_cell();
  if (e == WC_KEY_LEFT || e == WC_KEY_UP) stage_pick((k + ST_CELLS - 1) % ST_CELLS);
  else if (e == WC_KEY_RIGHT || e == WC_KEY_DOWN || e == WC_KEY_TAB) stage_pick((k + 1) % ST_CELLS);
  else if (e == WC_KEY_ENTER || e == WC_KEY_KP_ENTER || e == WC_KEY_SPACE) { act("start"); return; }
  else if (e == WC_KEY_ESCAPE || e == WC_KEY_BACKSPACE) { act("stageback"); return; }
}
static void stage_draw(void) {
  static const tshadow SH = {1, {{1, 1, COL(C_INK)}}};
  hint_style hs = {15, 0.6f, COL(C_CREAM), &SH};
  hint_line_set(&st_hint, &hs, hint_pick("{LEFT}{RIGHT} STAGE   {A} FIGHT   {B} FIGHTERS", "[LEFT][RIGHT] STAGE   [ENTER] FIGHT   [ESC] FIGHTERS",
                                         "<CLICK> A STAGE, AGAIN TO FIGHT   <RIGHT CLICK> FIGHTERS", "<STICK> STAGE   <LIGHT> FIGHT   <KICK> FIGHTERS"));
  int cell = st_cell();
  if (cell < STAGE_N && stage_needs_load(cell) && st_prev_of == cell && st_prev.c) {
    L_upload(&st_prev);
    ov_draw(st_prev.t, (CK_W - (st_prev.w - 16)) / 2.f, 96, 1);
  }
  L_upload(&st_card); ov_draw(st_card.t, 60, 470, 1);
  L_upload(&st_strip); ov_draw(st_strip.t, st_x(0) - 6, ST_Y - 6, 1);
  hint_line_draw(&st_hint, 640, 702, 1);
}

/* ================================================================== the tourney setup */
/* Rows: the size, the stage, an entrant each, then the buttons. On an entrant, A opens the fighter
 * grid (ui_select.c as a modal), left and right change the weapon and Y who plays. The focus shows
 * from the start, on the first entrant. */
static rlayer ts_R; static int ts_focus = 2, ts_btn = 1;
#define TS_W 1040
#define TS_ROW 36
static int ts_rows(void) { return TT ? 2 + TT->size + 1 : 1; }
static const char *stage_label(int s) { return s < 0 ? "A NEW ONE EACH MATCH" : STAGES[s].name; }
static void entrant_tag(const tourney_t *t, int i, char *out, int cap) {
  if (t->e[i].ctl != TC_PLAYER) { snprintf(out, cap, "CPU"); return; }
  int k = 1; for (int j = 0; j < i; j++) k += t->e[j].ctl == TC_PLAYER;
  snprintf(out, cap, "P%d", k);
}
static const char *LEVEL_OF[TC_N] = {"", "ROOKIE", "JOURNEYMAN", "EXTREME"};
/* a value between two arrows, each arrow a 40 px target */
static void arrows_paint(cv *c, float x, float y, float w, float h, bool on, int kind, int a) {
  arrow(c, x + 14, y + h / 2, -1, COLA(C_INK, on ? 1 : 0.45f));
  arrow(c, x + w - 14, y + h / 2, 1, COLA(C_INK, on ? 1 : 0.45f));
  hit_add(x, y, 40, h, kind, a, -1, NULL);
  hit_add(x + w - 40, y, 40, h, kind, a, 1, NULL);
}
static float ts_x0(void) { return (CK_W - TS_W) / 2.f; }
static void tsetup_paint(void) {
  nhits = 0;
  if (!TT) return;
  tourney_t *t = TT;
  float W = TS_W, rowH = TS_ROW;
  float H = 22 + 41 + 12 + 36 + 8 + 36 + 14 + 22 + t->size * rowH + 10 + 26 + 64 + 22;
  float x0 = ts_x0(), top = fmaxf(90, (CK_H - H) / 2 + 30), cx = x0 + 26, cy = top + 22, IW = W - 52;
  R_begin(&ts_R, (int)x0 - 12, (int)top - 12, (int)W + 30, (int)H + 30);
  cv *c = ts_R.L.c;
  c_box(c, x0, top, W, H, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(C_PINK));
  t_draw(c, F_MARKER, 30, 0, cx, t_base(F_MARKER, 30, cy, 30), "TOURNEY SETUP", COL(C_INK));
  char hum[48]; int nh = tourney_humans(t);
  snprintf(hum, sizeof hum, "%d PLAYER%s, %d CPU", nh, nh == 1 ? "" : "S", t->size - nh);
  t_draw(c, F_TYPE, 15, 1.5f, cx + IW - t_width(F_TYPE, 15, 1.5f, hum), cy + 12 + t_asc(F_TYPE, 15), hum, COLA(C_INK, 0.8f));
  c_rect(c, cx, cy + 38, IW, 3, COL(C_INK));
  float y = cy + 41 + 12;
  bool ring = input_last_dev != DEV_MOUSE;
  /* the size */
  t_draw(c, F_TYPE, 15, 2.4f, cx, y + 23, "ENTRANTS", COL(C_INK));
  float bx = cx + 170;
  for (int k = 0; k < 2; k++) {
    const char *n = k ? "8" : "4";
    chip_paint(c, bx, y, n, t->size == (k ? 8 : 4));
    hit_add(bx, y, chip_w(n), 34, H_TSIZE, k ? 8 : 4, 0, NULL);
    bx += chip_w(n) + 8;
  }
  t_draw(c, F_TYPE, 14, 1.4f, bx + 14, y + 22, "SINGLE ELIMINATION, BEST OF THREE ROUNDS", COLA(C_INK, 0.75f));
  if (ts_focus == 0 && ring) c_dash_rect(c, cx - 6, y - 4, IW + 12, 42, 3, COL(C_PINK));
  y += 36 + 8;
  /* the stage */
  t_draw(c, F_TYPE, 15, 2.4f, cx, y + 23, "STAGE", COL(C_INK));
  const char *sl = stage_label(t->stage);
  float sw = t_width(F_MARKER, 20, 0, sl) + 80;
  arrows_paint(c, cx + 160, y, sw, 34, ts_focus == 1, H_TSTAGE, 0);
  t_draw(c, F_MARKER, 20, 0, cx + 200, y + 25, sl, COL(C_INK));
  if (ts_focus == 1 && ring) c_dash_rect(c, cx - 6, y - 4, IW + 12, 42, 3, COL(C_PINK));
  y += 36 + 14;
  /* the entrants: who plays, the fighter (a click opens the grid), the weapon */
  float wx = cx, fx = cx + 210, gx = cx + 640;
  t_draw(c, F_TYPE, 14, 2.2f, wx, y + 14, "WHO PLAYS", COLA(C_INK, 0.75f));
  t_draw(c, F_TYPE, 14, 2.2f, fx, y + 14, "FIGHTER", COLA(C_INK, 0.75f));
  t_draw(c, F_TYPE, 14, 2.2f, gx, y + 14, "WEAPON", COLA(C_INK, 0.75f));
  y += 22;
  for (int i = 0; i < t->size; i++) {
    float ry = y + i * rowH, rh = rowH - 4;
    const t_entrant *E = &t->e[i];
    const roster_entry *R = roster_get(E->fighter);
    bool f = ts_focus == 2 + i;
    c_rect(c, cx, ry, IW, rh, f ? COLA(C_PINK, 0.3f) : COL(C_PAPER));
    c_border(c, cx, ry, IW, rh, 2, COL(C_INK));
    /* who plays: P1.. in ink, or the CPU and its level */
    char tag[24]; entrant_tag(t, i, tag, 8);
    if (E->ctl != TC_PLAYER) snprintf(tag, sizeof tag, "CPU %s", LEVEL_OF[E->ctl]);
    bool pl = E->ctl == TC_PLAYER;
    c_box(c, wx + 6, ry + 5, 190, rh - 10, pl ? COL(C_INK) : COL(C_CREAM), 2, COL(C_INK), 0, 0, COL(0));
    t_draw(c, pl ? F_LCD : F_TYPE, pl ? 18 : 14, 0, wx + 16, ry + rh / 2 + (pl ? 7 : 5), tag, pl ? COL(C_CREAM) : COL(C_INK));
    hit_add(wx + 6, ry + 5, 190, rh - 10, H_TCTL, i, 0, NULL);
    /* the fighter: its stripe, face and name */
    c_rect(c, fx - 6, ry + 2, 6, rh - 4, COL(R->color));
    c_rect(c, fx + 2, ry + 3, rh - 6, rh - 6, COL(C_PAPER));
    select_face(c, E->fighter, fx + 2, ry + 3, rh - 6, rh - 6);
    float fs = fit_fs(R->name, 20, 330, 15);
    t_draw(c, F_MARKER, fs, 0, fx + rh + 8, ry + rh / 2 + fs * 0.36f, R->name, COL(C_INK));
    hit_add(fx - 6, ry, 420, rh, H_TFIGHTER, i, 0, NULL);
    /* the weapon, between arrows */
    arrows_paint(c, gx - 4, ry, IW - (gx - cx) + 4, rh, f, H_TWEAPON, i);
    t_draw(c, F_TYPE, 15, 0.8f, gx + 36, ry + rh / 2 + 5, WEAPONS[E->weapon].name, COL(C_INK));
    if (f && ring) c_dash_rect(c, cx - 5, ry - 4, IW + 10, rh + 8, 3, COL(C_PINK));
  }
  y += t->size * rowH + 10;
  /* the prompt, for the device in use */
  static const tshadow NS = {0};
  hint_style hs = {15, 0.6f, COL(C_INK), &NS};
  const char *hp = hint_pick("{A} PICK THE FIGHTER   {LEFT}{RIGHT} THE WEAPON   {Y} WHO PLAYS   {START} BEGIN",
                             "[ENTER] PICK THE FIGHTER   [LEFT][RIGHT] THE WEAPON   [TAB] WHO PLAYS",
                             "<CLICK> A FIGHTER, A WEAPON'S ARROWS OR WHO PLAYS TO CHANGE IT",
                             "<LIGHT> PICK THE FIGHTER   <STICK> THE WEAPON   <DODGE> WHO PLAYS");
  while (hs.fs > 14 && hint_width(&hs, hp) > IW) hs.fs -= 0.5f;
  hint_paint(c, &hs, cx, y + 12, hp);
  y += 26;
  /* the buttons */
  bool fb = ts_focus == ts_rows() - 1;
  btn_t sh; btn_init(&sh, cx + IW - 360 - 14 - 250, y, 250, 64, 22, 16, 16, BTN_GHOST, "SHUFFLE CPUS", "tshuffle");
  sh.center = true;
  btn_paint_at(c, &sh, fb && ts_btn == 0 ? 1 : 0, 0, 0);
  hit_add(sh.x, sh.y, sh.w, sh.h, H_ACT, 0, 0, "tshuffle");
  btn_t go; btn_init(&go, cx + IW - 360, y, 360, 64, 34, 22, 10, BTN_GO, "START TOURNEY", "tstart");
  btn_paint_at(c, &go, fb && ts_btn == 1 ? 1 : 0, 0, 0);
  hit_add(go.x, go.y, go.w, go.h, H_ACT, 0, 0, "tstart");
}
static void tsetup_act(const char *a) {
  if (!strcmp(a, "tshuffle")) { tourney_shuffle_cpus(TT); sfx_ui(); tsetup_paint(); return; }
  act(a);
}
static int ts_grid_for = -1;
static void ts_grid_done(int f) {
  if (f >= 0 && ts_grid_for >= 0) { tourney_set_fighter(TT, ts_grid_for, f); sfx_ui_ok(); }
  select_modal_close();
  ts_grid_for = -1;
  tsetup_paint();
}
static void ts_open_grid(int i) {
  char t[48], tag[8]; entrant_tag(TT, i, tag, sizeof tag);
  snprintf(t, sizeof t, "ENTRANT %d, %s", i + 1, tag);
  ts_grid_for = i; ts_focus = 2 + i;
  sfx_ui();
  select_modal_open(TT->e[i].fighter, t, ts_grid_done);
}
static void tsetup_key(int e) {
  if (!TT) return;
  int n = ts_rows(), r = ts_focus;
  kbd_nav = true;
  if (e == CK_KEY_START) { tsetup_act("tstart"); return; }
  if (e == WC_KEY_UP) { ts_focus = (r + n - 1) % n; sfx_ui(); }
  else if (e == WC_KEY_DOWN) { ts_focus = (r + 1) % n; sfx_ui(); }
  else if (e == WC_KEY_LEFT || e == WC_KEY_RIGHT) {
    int d = e == WC_KEY_RIGHT ? 1 : -1;
    if (r == 0) tourney_set_size(TT, TT->size == 4 ? 8 : 4);
    else if (r == 1) { TT->stage += d; if (TT->stage < -1) TT->stage = STAGE_N - 1; if (TT->stage >= STAGE_N) TT->stage = -1; }
    else if (r < n - 1) tourney_cycle_weapon(TT, r - 2, d);
    else ts_btn ^= 1;
    sfx_ui();
  } else if (e == WC_KEY_TAB) { if (r >= 2 && r < n - 1) { tourney_cycle_ctl(TT, r - 2, 1); sfx_ui(); } }
  else if (e == WC_KEY_ENTER || e == WC_KEY_KP_ENTER || e == WC_KEY_SPACE) {
    if (r >= 2 && r < n - 1) { ts_open_grid(r - 2); return; }
    else if (r == n - 1) { tsetup_act(ts_btn ? "tstart" : "tshuffle"); return; }
    else if (r == 0) { tourney_set_size(TT, TT->size == 4 ? 8 : 4); sfx_ui(); }
    else { ts_focus = 2; sfx_ui(); }
  } else if (e == WC_KEY_ESCAPE) { act("back"); return; }
  else return;
  if (ts_focus >= ts_rows()) ts_focus = ts_rows() - 1;
  tsetup_paint();
}

/* ================================================================== the bracket */
static rlayer br_R; static int br_btn; static bool br_confirm;   /* QUIT pressed once: press again to leave */
static int br_buttons(const char **acts, const char **labels) {
  int n = 0;
  if (!TT || TT->cur < 0) return 0;
  bool cpu = tourney_cpu_only(TT, TT->cur);
  acts[n] = "tplay"; labels[n++] = cpu ? "WATCH" : "FIGHT!";
  if (cpu) { acts[n] = "tskip"; labels[n++] = "SKIP"; }
  acts[n] = "tquit"; labels[n++] = br_confirm ? "SURE? QUIT" : "QUIT";
  return n;
}
/* who takes which controls for a match: two people split them (pad 1 or WASD on the left, pad 2
 * or the arrows on the right); one person against the CPU plays on any of them */
static void controls_line(const tourney_t *t, int m, char *out, int cap) {
  const t_match *M = &t->m[m];
  char ta[8], tb[8]; entrant_tag(t, M->a, ta, sizeof ta); entrant_tag(t, M->b, tb, sizeof tb);
  bool ha = t->e[M->a].ctl == TC_PLAYER, hb = t->e[M->b].ctl == TC_PLAYER;
  bool keys = input_last_dev == DEV_KEYS || input_last_dev == DEV_MOUSE;
  if (ha && hb) snprintf(out, cap, keys ? "%s ON THE LEFT (WASD), %s ON THE RIGHT (ARROWS)" : "%s TAKES PAD 1 (LEFT), %s TAKES PAD 2 (RIGHT)", ta, tb);
  else if (ha || hb) snprintf(out, cap, "%s PLAYS ON ANY PAD OR THE KEYBOARD", ha ? ta : tb);
  else snprintf(out, cap, "THE CPU PLAYS BOTH SIDES: WATCH IT OR SKIP IT");
}
static void name_box(cv *c, const tourney_t *t, int ent, float x, float y, float w, int wins, int state) {
  /* state: 0 waiting, 1 won, 2 lost */
  c_rect(c, x, y, w, 30, state == 1 ? COL(C_PAPER) : state == 2 ? COLA(C_PAPER, 0.55f) : COL(C_PAPER));
  c_border(c, x, y, w, 30, 2, COL(C_INK));
  if (ent < 0) { t_draw(c, F_TYPE, 14, 1.4f, x + 10, y + 7 + t_asc(F_TYPE, 14), "TO BE DECIDED", COLA(C_INK, 0.5f)); return; }
  const roster_entry *R = roster_get(t->e[ent].fighter);
  c_rect(c, x, y, 6, 30, COL(R->color));
  char tag[8]; entrant_tag(t, ent, tag, sizeof tag);
  col_t ink = state == 2 ? COLA(C_INK, 0.45f) : COL(C_INK);
  t_draw(c, F_LCD, 15, 0, x + 12, y + 7 + t_asc(F_LCD, 15), tag, ink);
  float fs = fit_fs(R->name, 17, w - 86, 13);
  t_draw(c, F_MARKER, fs, 0, x + 50, y + 15 + fs * 0.36f, R->name, ink);
  if (state) {
    char wn[4]; snprintf(wn, sizeof wn, "%d", wins);
    c_rect(c, x + w - 26, y + 4, 22, 22, state == 1 ? COL(C_PINK) : COLA(C_INK, 0.15f));
    t_draw(c, F_LCD, 14, 0, x + w - 20, y + 6 + t_asc(F_LCD, 14), wn, ink);
  }
}
static void bracket_paint(void) {
  nhits = 0;
  if (!TT || !TT->started) return;
  const tourney_t *t = TT;
  int R = tourney_rounds(t);
  float W = 1160, H = 520, x0 = 60, top = 92;
  R_begin(&br_R, (int)x0 - 12, (int)top - 12, (int)W + 30, (int)H + 30);
  cv *c = br_R.L.c;
  c_box(c, x0, top, W, H, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(C_PINK));
  float cx = x0 + 26, cy = top + 20, IW = W - 52;
  /* the columns: each round, then the champion */
  float colw = (IW - 40) / (R + 1), boxw = colw - 46, area_top = cy + 40, area_h = H - 20 - 40 - 110;
  for (int r = 0; r <= R; r++) {
    float x = cx + r * (colw + 10);
    const char *nm = r < R ? tourney_round_name(t, r) : "CHAMPION";
    lab_paint(c, x, cy + 6, nm);
  }
  c_rect(c, cx, cy + 30, IW, 3, COL(C_INK));
  float pos[TOURNEY_MAX - 1];   /* each match's centre y */
  for (int r = 0; r < R; r++) {
    int first = tourney_round_first(t, r), nm = t->size >> (r + 1);
    float x = cx + r * (colw + 10);
    for (int k = 0; k < nm; k++) {
      int m = first + k;
      const t_match *M = &t->m[m];
      float slot = area_h / nm, my = area_top + slot * (k + .5f);
      pos[m] = my;
      int sa = M->winner < 0 ? 0 : M->winner == M->a ? 1 : 2, sb = M->winner < 0 ? 0 : M->winner == M->b ? 1 : 2;
      name_box(c, t, M->a, x, my - 32, boxw, M->wins_a, sa);
      name_box(c, t, M->b, x, my + 2, boxw, M->wins_b, sb);
      if (m == t->cur) c_dash_rect(c, x - 7, my - 39, boxw + 14, 78, 3, COL(C_PINK));
      /* the line on to the next round */
      if (r > 0) {
        float px = x - 10 - 18, a = pos[tourney_round_first(t, r - 1) + k * 2], b = pos[tourney_round_first(t, r - 1) + k * 2 + 1];
        c_rect(c, px, a, 3, b - a + 3, COL(C_INK));
        c_rect(c, px, my, 18, 3, COL(C_INK));
      }
      c_rect(c, x + boxw, my, 18, 3, COL(C_INK));
    }
  }
  /* the champion's box */
  {
    float x = cx + R * (colw + 10), my = pos[tourney_matches(t) - 1];
    c_rect(c, x - 28, my, 28, 3, COL(C_INK));
    if (t->champion >= 0) {
      c_box(c, x, my - 24, boxw, 48, COL(C_PINK), 3, COL(C_INK), 5, 5, COL(C_INK));
      const roster_entry *Rr = roster_get(t->e[t->champion].fighter);
      float fs = fit_fs(Rr->name, 22, boxw - 20, 12);
      t_draw(c, F_MARKER, fs, 0, x + 10, my - 24 + 12 + t_asc(F_MARKER, 22), Rr->name, COL(C_INK));
    } else {
      c_rect(c, x, my - 24, boxw, 48, COLA(C_PAPER, 0.6f));
      c_dash_rect(c, x, my - 24, boxw, 48, 2, COL(C_INK));
      t_draw(c, F_MARKER, 22, 0, x + 12, my - 24 + 12 + t_asc(F_MARKER, 22), "?", COLA(C_INK, 0.4f));
    }
  }
  /* the next match and the buttons */
  float by = top + H - 20 - 80;
  c_rect(c, cx, by - 12, IW, 3, COL(C_INK));
  if (t->cur >= 0) {
    const t_match *M = &t->m[t->cur];
    char l1[96], l2[128];
    int st = tourney_stage_of(t, t->cur, STAGE_N);
    snprintf(l1, sizeof l1, "NEXT: %s, %s", tourney_round_name(t, tourney_round_of(t, t->cur)), STAGES[st].name);
    char ta[8], tb[8]; entrant_tag(t, M->a, ta, sizeof ta); entrant_tag(t, M->b, tb, sizeof tb);
    snprintf(l2, sizeof l2, "%s (%s) VS %s (%s)", roster_get(t->e[M->a].fighter)->name, ta, roster_get(t->e[M->b].fighter)->name, tb);
    t_draw(c, F_TYPE, 14, 2.2f, cx, by + 4 + t_asc(F_TYPE, 14), l1, COL(C_INK));
    float fs = fit_fs(l2, 24, IW - 520, 16);
    t_draw(c, F_MARKER, fs, 0, cx, by + 24 + t_asc(F_MARKER, 24), l2, COL(C_INK));
    char l3[96]; controls_line(t, t->cur, l3, sizeof l3);
    t_draw(c, F_TYPE, 14, 0.7f, cx, by + 58 + t_asc(F_TYPE, 14), l3, COL(0x8a5a00));
  }
  const char *acts[3], *labels[3]; int nb = br_buttons(acts, labels);
  if (br_btn >= nb) br_btn = nb ? nb - 1 : 0;
  float bx = cx + IW;
  for (int i = nb - 1; i >= 0; i--) {
    float w = i == 0 ? 220 : 130;
    bx -= w;
    btn_t b; btn_init(&b, bx, by + 4, w, 60, i == 0 ? 32 : 24, 16, 12, i == 0 ? BTN_GO : BTN_GHOST, labels[i], acts[i]);
    b.center = true;
    btn_paint_at(c, &b, br_btn == i ? 1 : 0, 0, 0);
    hit_add(b.x, b.y, b.w, b.h, H_ACT, 0, 0, acts[i]);
    bx -= 14;
  }
}
static void bracket_key(int e) {
  const char *acts[3], *labels[3]; int nb = br_buttons(acts, labels);
  if (!nb) return;
  if (e == CK_KEY_START) { act("tplay"); return; }
  bool was = br_confirm;
  br_confirm = false;
  if ((e == WC_KEY_ENTER || e == WC_KEY_KP_ENTER || e == WC_KEY_SPACE) && !strcmp(acts[br_btn], "tquit") && !was) {
    br_confirm = true; sfx_ui(); bracket_paint(); return;
  }
  if (e == WC_KEY_LEFT || e == WC_KEY_UP) { br_btn = (br_btn + nb - 1) % nb; sfx_ui(); }
  else if (e == WC_KEY_RIGHT || e == WC_KEY_DOWN || e == WC_KEY_TAB) { br_btn = (br_btn + 1) % nb; sfx_ui(); }
  else if (e == WC_KEY_ENTER || e == WC_KEY_KP_ENTER || e == WC_KEY_SPACE) { act(acts[br_btn]); return; }
  else if (e == WC_KEY_ESCAPE) { br_btn = nb - 1; sfx_ui(); }
  else return;
  bracket_paint();
}

/* ================================================================== the champion */
static rlayer ch_R; static btn_t ch_b[2]; static int ch_idx;
static void champ_paint(void) {
  nhits = 0;
  if (!TT || TT->champion < 0) return;
  const tourney_t *t = TT;
  const roster_entry *R = roster_get(t->e[t->champion].fighter);
  char tag[8]; entrant_tag(t, t->champion, tag, sizeof tag);
  int won = 0, lost = 0;
  for (int m = 0; m < tourney_matches(t); m++) {
    const t_match *M = &t->m[m];
    if (M->a == t->champion) { won += M->wins_a; lost += M->wins_b; }
    if (M->b == t->champion) { won += M->wins_b; lost += M->wins_a; }
  }
  float W = 600, x = CK_W - 60 - W;
  float fs = fit_fs(R->name, 86, W - 52, 40);
  float H = 4 + 20 + 34 + 8 + fs * 0.95f + 12 + 22 + 8 + 20 + 18 + 2 * 73 + 4;
  float top = (CK_H - H) / 2 + 20;
  R_begin(&ch_R, (int)x - 10, (int)top - 10, (int)W + 30, (int)H + 30);
  cv *c = ch_R.L.c;
  c_box(c, x, top, W, H, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(C_PINK));
  float cx = x + 26, y = top + 24;
  t_draw(c, F_MARKER, 30, 0, cx, t_base(F_MARKER, 30, y, 34), "TOURNEY CHAMPION", COL(C_PINK));
  y += 34 + 8;
  t_draw(c, F_MARKER, fs, 0, cx, t_base(F_MARKER, fs, y, fs * 0.95f), R->name, COL(C_INK));
  y += fs * 0.95f + 12;
  char l[96]; snprintf(l, sizeof l, "%s. %s", tag, R->title);
  t_draw(c, F_TYPE, 16, 2.88f, cx, y + t_asc(F_TYPE, 16), l, COL(C_INK));
  y += 22 + 8;
  snprintf(l, sizeof l, "%d ENTRANTS, %d MATCHES, ROUNDS %d TO %d", t->size, tourney_rounds(t), won, lost);
  t_draw(c, F_TYPE, 13, 1.82f, cx, y + t_asc(F_TYPE, 13), l, COLA(C_INK, 0.75f));
  y += 20 + 18;
  static const char *lab[2] = {"NEW TOURNEY", "MAIN MENU"}, *ac[2] = {"tnew", "quit"};
  for (int i = 0; i < 2; i++) {
    bool keep = ch_b[i].sel; trans_t tr = ch_b[i].t; layer L = ch_b[i].L;
    btn_init(&ch_b[i], cx, y + i * 73, W - 52, 57, 32, 22, 8, i == 0 ? BTN_NORMAL : BTN_GHOST, lab[i], ac[i]);
    ch_b[i].sel = keep; ch_b[i].t = tr; ch_b[i].L = L; ch_b[i].painted = -1;
  }
}

/* ================================================================== the module */
void menus_init(void) {
  select_init();
  stills_init();
  head_paint(&HD_SEL, "PICK YOUR POISON");
  head_paint(&HD_STAGE, "PICK THE STAGE");
  head_paint(&HD_TOURNEY, "TOURNEY");
}
bool menus_owns(const char *id) {
  return id && (!strcmp(id, "s-select") || !strcmp(id, "s-stage") || !strcmp(id, "s-tsetup") || !strcmp(id, "s-bracket") || !strcmp(id, "s-champ"));
}
static void repaint(void) {
  const char *s = scr_cur;
  if (!menus_owns(s)) return;
  if (!strcmp(s, "s-stage")) stage_paint();
  else if (!strcmp(s, "s-tsetup")) tsetup_paint();
  else if (!strcmp(s, "s-bracket")) bracket_paint();
  else if (!strcmp(s, "s-champ")) champ_paint();
}
void ui_menus_changed(void) { repaint(); }
void menus_show(const char *id) {
  if (!strcmp(id, "s-select")) select_show();
  if (!strcmp(id, "s-stage")) st_prev_of = -2;
  if (!strcmp(id, "s-tsetup")) { ts_focus = 2; ts_btn = 1; }
  if (!strcmp(id, "s-bracket")) { br_btn = 0; br_confirm = false; }
  if (!strcmp(id, "s-champ")) { ch_idx = 0; for (int i = 0; i < 2; i++) { ch_b[i].sel = i == 0; tr_jump(&ch_b[i].t, i == 0 ? 1 : 0); ch_b[i].painted = -1; } }
  repaint();
}
static void menus_draw_screen(const char *s);
static void hover_draw(void);
void menus_draw(void) {
  const char *s = scr_cur;
  menus_draw_screen(s);
  if (strcmp(s, "s-select") && strcmp(s, "s-champ")) hover_draw();
}
static void menus_draw_screen(const char *s) {
  if (!strcmp(s, "s-select")) { draw_shade_v(); draw_back(); head_draw(&HD_SEL); select_draw(); return; }
  if (!strcmp(s, "s-stage")) { draw_shade_v(); draw_back(); head_draw(&HD_STAGE); stage_draw(); return; }
  if (!strcmp(s, "s-tsetup")) { ov_fill(0, 0, CK_W, CK_H, 0x0a0a0a, 0.62f); draw_back(); head_draw(&HD_TOURNEY); R_draw(&ts_R); return; }
  if (!strcmp(s, "s-bracket")) { ov_fill(0, 0, CK_W, CK_H, 0x0a0a0a, 0.62f); head_draw(&HD_TOURNEY); R_draw(&br_R); return; }
  if (!strcmp(s, "s-champ")) { draw_shade_h(); R_draw(&ch_R); for (int i = 0; i < 2; i++) btn_draw(&ch_b[i]); return; }
}
void menus_key(int e) {
  const char *s = scr_cur;
  if (!strcmp(s, "s-select")) select_key(e);
  else if (!strcmp(s, "s-stage")) stage_key(e);
  else if (!strcmp(s, "s-tsetup")) tsetup_key(e);
  else if (!strcmp(s, "s-bracket")) bracket_key(e);
  else if (!strcmp(s, "s-champ")) {
    if (e == WC_KEY_UP || e == WC_KEY_DOWN || e == WC_KEY_W || e == WC_KEY_S) {
      ch_idx ^= 1; for (int i = 0; i < 2; i++) btn_set_sel(&ch_b[i], i == ch_idx); sfx_ui();
    } else if (e == WC_KEY_ENTER || e == WC_KEY_KP_ENTER || e == WC_KEY_SPACE) act(ch_b[ch_idx].act);
  }
}

/* the hit rect under the mouse, outlined so a click's target shows before it lands */
static int hover_hit = -1;
static void hover_draw(void) {
  if (hover_hit < 0 || hover_hit >= nhits || input_last_dev != DEV_MOUSE) return;
  const hit_t *h = &hits[hover_hit];
  float x = h->x - 4, y = h->y - 4, w = h->w + 8, hh = h->h + 8;
  ov_fill(x, y, w, 3, C_PINK, 1); ov_fill(x, y + hh - 3, w, 3, C_PINK, 1); ov_fill(x, y, 3, hh, C_PINK, 1); ov_fill(x + w - 3, y, 3, hh, C_PINK, 1);
}
static void click(const hit_t *h) {
  const char *s = scr_cur;
  switch (h->kind) {
    case H_ACT:
      if (!strcmp(s, "s-tsetup")) tsetup_act(h->s);
      else if (!strcmp(h->s, "tquit") && !br_confirm) { br_confirm = true; sfx_ui(); bracket_paint(); }
      else { br_confirm = false; act(h->s); }
      return;
    case H_STAGE: if (h->a - 100 == st_cell()) { act("start"); return; } stage_pick(h->a - 100); break;
    case H_TSIZE: tourney_set_size(TT, h->a); sfx_ui(); break;
    case H_TSTAGE: TT->stage += h->b; if (TT->stage < -1) TT->stage = STAGE_N - 1; if (TT->stage >= STAGE_N) TT->stage = -1; ts_focus = 1; sfx_ui(); break;
    case H_TCTL: tourney_cycle_ctl(TT, h->a, 1); ts_focus = 2 + h->a; sfx_ui(); break;
    case H_TFIGHTER: ts_open_grid(h->a); return;
    case H_TWEAPON: tourney_cycle_weapon(TT, h->a, h->b); ts_focus = 2 + h->a; sfx_ui(); break;
  }
  repaint();
}
bool ui_reads_pads(void) { return select_modal_active() || (scr_cur && !strcmp(scr_cur, "s-select")); }
void menus_frame(float dt) {
  const char *s = scr_cur;
  if (s && !strcmp(s, "s-select")) select_frame(dt);
}
void menus_pointer(float x, float y, bool pressed) {
  const char *s = scr_cur;
  if (!strcmp(s, "s-select")) { select_pointer(x, y, pressed); return; }
  if (!strcmp(s, "s-champ")) {
    for (int i = 0; i < 2; i++) if (btn_hit(&ch_b[i], x, y) && i != ch_idx) { ch_idx = i; for (int k = 0; k < 2; k++) btn_set_sel(&ch_b[k], k == ch_idx); }
    if (pressed) for (int i = 0; i < 2; i++) if (btn_hit(&ch_b[i], x, y)) { act(ch_b[i].act); return; }
    return;
  }
  /* the mouse over a screen: BACK and whatever can be clicked light up; a right click goes back */
  const ck_pointer *P = input_pointer();
  bool has_back = !strcmp(s, "s-stage") || !strcmp(s, "s-tsetup");
  bool on_back = has_back && btn_hit(&back_b, x, y);
  if (has_back && on_back != back_b.sel) btn_set_sel(&back_b, on_back);
  hover_hit = -1;
  for (int i = 0; i < nhits; i++) if (x >= hits[i].x && x < hits[i].x + hits[i].w && y >= hits[i].y && y < hits[i].y + hits[i].h) { hover_hit = i; break; }
  if (P->rpressed && has_back) { act(!strcmp(s, "s-stage") ? "stageback" : "back"); return; }
  if (!pressed) return;
  if (on_back) { act(!strcmp(s, "s-stage") ? "stageback" : "back"); return; }
  if (hover_hit >= 0) { hit_t h = hits[hover_hit]; click(&h); }
}

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
#include "ck.h"
#include "audio.h"
#include "input.h"
#include "roster.h"
#include "weapons.h"
#include "stage.h"
#include "special.h"
#include "tourney.h"
#include <stdio.h>
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
static rlayer st_R; static int st_focus;   /* 0 the stages, 1 FIGHT */
static void stage_paint(void) {
  nhits = 0;
  if (!scr_sel) return;
  int s = scr_sel->stage; if (s < 0 || s >= STAGE_N) s = 0;
  const stage_def *d = &STAGES[s];
  float px = 80, W = 1120, H = 196, top = CK_H - 18 - H;
  R_begin(&st_R, 64, (int)top - 14, 1160, CK_H - (int)top + 14);
  cv *c = st_R.L.c;
  c_box(c, px, top, W, H, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(C_PINK));
  if (st_focus == 0) c_dash_rect(c, px - 9, top - 9, W + 18, H + 18, 4, COL(C_PINK));
  float ix = px + 24, iy = top + 18, IW = W - 48 - 340;
  lab_paint(c, ix, iy, "STAGE");
  char cnt[16]; snprintf(cnt, sizeof cnt, "%d / %d", s + 1, STAGE_N);
  t_draw(c, F_LCD, 14, 0, ix + IW - t_width(F_LCD, 14, 0, cnt), iy + t_asc(F_LCD, 14), cnt, COLA(C_INK, 0.6f));
  float fs = fit_fs(d->name, 46, IW - 60, 26);
  arrow(c, ix + 8, iy + 50, -1, COL(C_INK));
  t_draw(c, F_MARKER, fs, 0, ix + 30, t_base(F_MARKER, fs, iy + 26, 48), d->name, COL(C_INK));
  arrow(c, ix + 30 + t_width(F_MARKER, fs, 0, d->name) + 22, iy + 50, 1, COL(C_INK));
  hit_add(ix - 6, iy + 30, 30, 40, H_STAGE, -1, 0, NULL);
  hit_add(ix + 30 + t_width(F_MARKER, fs, 0, d->name) + 8, iy + 30, 30, 40, H_STAGE, 1, 0, NULL);
  char bl[2][160]; int nb = t_wrap(F_TYPE, 14, 0, d->blurb, IW, false, bl, 2);
  for (int k = 0; k < nb; k++) t_draw(c, F_TYPE, 14, 0, ix, t_base(F_TYPE, 14, iy + 82 + k * 19, 19), bl[k], COL(C_INK));
  const char *note = d->web ? "THE WEB VERSION'S OWN SHOP" : stage_needs_load(s) ? "FROM KEYHOLDER. LOADS ON FIGHT" : "FROM KEYHOLDER";
  t_draw(c, F_TYPE, 11, 1.98f, ix, iy + 82 + nb * 19 + 8 + t_asc(F_TYPE, 11), note, COLA(C_INK, 0.7f));
  /* the strip: every stage */
  float sw = (IW - (STAGE_N - 1) * 6) / STAGE_N, sy = top + H - 18 - 26;
  for (int k = 0; k < STAGE_N; k++) {
    float bx = ix + k * (sw + 6);
    bool on = k == s;
    c_rect(c, bx, sy, sw, 26, on ? COL(C_PINK) : COL(C_PAPER));
    c_border(c, bx, sy, sw, 26, 2, COL(C_INK));
    char n[4]; snprintf(n, sizeof n, "%d", k + 1);
    t_draw(c, F_LCD, 13, 0, bx + (sw - t_width(F_LCD, 13, 0, n)) / 2, sy + 5 + t_asc(F_LCD, 13), n, COL(C_INK));
    hit_add(bx, sy, sw, 26, H_STAGE, 100 + k, 0, NULL);
  }
  btn_t go; btn_init(&go, px + W - 24 - 320, top + (H - 64) / 2, 320, 64, 38.4f, 22, 8, BTN_GO, "FIGHT!", "start");
  btn_paint_at(c, &go, st_focus == 1 ? 1 : 0, 0, 0);
  hit_add(go.x, go.y, 320, 64, H_ACT, 0, 0, "start");
}
static void stage_pick(int d, bool absolute) {
  int s = absolute ? d : (scr_sel->stage + d + STAGE_N) % STAGE_N;
  if (ui_cb.stage) ui_cb.stage(s);
  sfx_ui();
}
static void stage_key(int e) {
  if (e == WC_KEY_UP || e == WC_KEY_DOWN || e == WC_KEY_TAB) { st_focus ^= 1; sfx_ui(); }
  else if (e == WC_KEY_LEFT || e == WC_KEY_RIGHT) { if (st_focus == 0) stage_pick(e == WC_KEY_RIGHT ? 1 : -1, false); else { st_focus = 0; sfx_ui(); } }
  else if (e == WC_KEY_ENTER || e == WC_KEY_KP_ENTER) { if (st_focus == 1) { act("start"); return; } st_focus = 1; sfx_ui(); }
  else if (e == WC_KEY_ESCAPE) { act("stageback"); return; }
  else return;
  stage_paint();
}

/* ================================================================== the tourney setup */
static rlayer ts_R; static int ts_focus, ts_btn;   /* rows: 0 size, 1 stage, 2.. entrants, then the buttons */
static int ts_rows(void) { return TT ? 2 + TT->size + 1 : 1; }
static const char *stage_label(int s) { return s < 0 ? "A NEW ONE EACH MATCH" : STAGES[s].name; }
static void entrant_tag(const tourney_t *t, int i, char *out, int cap) {
  if (t->e[i].ctl != TC_PLAYER) { snprintf(out, cap, "CPU"); return; }
  int k = 1; for (int j = 0; j < i; j++) k += t->e[j].ctl == TC_PLAYER;
  snprintf(out, cap, "P%d", k);
}
static void tsetup_paint(void) {
  nhits = 0;
  if (!TT) return;
  tourney_t *t = TT;
  float W = 1000, rowH = 38;
  float H = 22 + 41 + 12 + 34 + 10 + 34 + 14 + t->size * rowH + 10 + 64 + 22;
  float x0 = (CK_W - W) / 2, top = fmaxf(88, (CK_H - H) / 2 + 30), cx = x0 + 26, cy = top + 22, IW = W - 52;
  R_begin(&ts_R, (int)x0 - 12, (int)top - 12, (int)W + 30, (int)H + 30);
  cv *c = ts_R.L.c;
  c_box(c, x0, top, W, H, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(C_PINK));
  t_draw(c, F_MARKER, 30, 0, cx, t_base(F_MARKER, 30, cy, 30), "BRACKET", COL(C_INK));
  char hum[48]; snprintf(hum, sizeof hum, "%d PLAYER%s, %d CPU", tourney_humans(t), tourney_humans(t) == 1 ? "" : "S", t->size - tourney_humans(t));
  t_draw(c, F_TYPE, 13, 1.82f, cx + IW - t_width(F_TYPE, 13, 1.82f, hum), cy + 12 + t_asc(F_TYPE, 13), hum, COLA(C_INK, 0.7f));
  c_rect(c, cx, cy + 38, IW, 3, COL(C_INK));
  float y = cy + 41 + 12;
  /* size */
  lab_paint(c, cx, y + 8.5f, "ENTRANTS");
  float bx = cx + 150;
  for (int k = 0; k < 2; k++) {
    const char *n = k ? "8" : "4";
    chip_paint(c, bx, y, n, t->size == (k ? 8 : 4));
    hit_add(bx, y, chip_w(n), 34, H_TSIZE, k ? 8 : 4, 0, NULL);
    bx += chip_w(n) + 8;
  }
  t_draw(c, F_TYPE, 12, 1.2f, bx + 14, y + 10 + t_asc(F_TYPE, 12), "SINGLE ELIMINATION, BEST OF THREE ROUNDS", COLA(C_INK, 0.7f));
  if (ts_focus == 0 && kbd_nav) focus_ring(c, cx - 4, y - 2, IW + 8, 38);
  y += 34 + 10;
  /* stage */
  lab_paint(c, cx, y + 8.5f, "STAGE");
  const char *sl = stage_label(t->stage);
  arrow(c, cx + 158, y + 17, -1, COL(C_INK));
  t_draw(c, F_MARKER, 20, 0, cx + 176, y + 5 + t_asc(F_MARKER, 20), sl, COL(C_INK));
  arrow(c, cx + 176 + t_width(F_MARKER, 20, 0, sl) + 18, y + 17, 1, COL(C_INK));
  hit_add(cx + 146, y, 26, 34, H_TSTAGE, -1, 0, NULL);
  hit_add(cx + 176 + t_width(F_MARKER, 20, 0, sl) + 6, y, 26, 34, H_TSTAGE, 1, 0, NULL);
  if (ts_focus == 1 && kbd_nav) focus_ring(c, cx - 4, y - 2, IW + 8, 38);
  y += 34 + 14;
  /* the entrants */
  for (int i = 0; i < t->size; i++) {
    float ry = y + i * rowH;
    const t_entrant *E = &t->e[i];
    const roster_entry *R = roster_get(E->fighter);
    bool f = ts_focus == 2 + i;
    c_rect(c, cx, ry, IW, rowH - 6, f ? COLA(C_PINK, 0.35f) : COL(C_PAPER));
    c_border(c, cx, ry, IW, rowH - 6, 2, COL(C_INK));
    c_rect(c, cx, ry, 8, rowH - 6, COL(R->color));
    char tag[8]; entrant_tag(t, i, tag, sizeof tag);
    t_draw(c, F_LCD, 16, 0, cx + 18, ry + 6 + t_asc(F_LCD, 16), tag, COL(C_INK));
    /* who plays: a chip that cycles */
    const char *cn = tourney_ctl_name(E->ctl);
    float chx = cx + 74;
    c_box(c, chx, ry + 4, 190, rowH - 14, E->ctl == TC_PLAYER ? COL(C_INK) : COL(C_CREAM), 2, COL(C_INK), 0, 0, COL(0));
    t_draw(c, F_TYPE, 12, 1.2f, chx + 10, ry + 9 + t_asc(F_TYPE, 12), cn, E->ctl == TC_PLAYER ? COL(C_CREAM) : COL(C_INK));
    hit_add(chx, ry + 4, 190, rowH - 14, H_TCTL, i, 0, NULL);
    /* the fighter, with arrows */
    float fx = chx + 210;
    arrow(c, fx + 6, ry + 16, -1, COLA(C_INK, f ? 1 : 0.4f));
    float fs = fit_fs(R->name, 20, 260, 14);
    t_draw(c, F_MARKER, fs, 0, fx + 22, ry + 5 + t_asc(F_MARKER, 20), R->name, COL(C_INK));
    arrow(c, fx + 296, ry + 16, 1, COLA(C_INK, f ? 1 : 0.4f));
    hit_add(fx - 4, ry, 24, rowH - 6, H_TFIGHTER, i, -1, NULL);
    hit_add(fx + 284, ry, 24, rowH - 6, H_TFIGHTER, i, 1, NULL);
    /* the weapon: click (or Tab) for the next */
    float wx = fx + 320;
    const char *wn = WEAPONS[E->weapon].name;
    t_draw(c, F_TYPE, 13, 1.3f, wx, ry + 9 + t_asc(F_TYPE, 13), wn, COL(C_INK));
    hit_add(wx, ry, IW - (wx - cx), rowH - 6, H_TWEAPON, i, 0, NULL);
  }
  y += t->size * rowH + 10;
  /* the buttons */
  const char *hint = "UP AND DOWN PICK A ROW. LEFT AND RIGHT CHANGE THE FIGHTER. ENTER: WHO PLAYS. TAB: WEAPON.";
  char hl[3][160]; int nh = t_wrap(F_TYPE, 11, 1.1f, hint, IW - 360 - 14 - 230 - 20, false, hl, 3);
  for (int k = 0; k < nh; k++) t_draw(c, F_TYPE, 11, 1.1f, cx, y + 16 + k * 15 + t_asc(F_TYPE, 11), hl[k], COLA(C_INK, 0.65f));
  bool fb = ts_focus == ts_rows() - 1;
  btn_t sh; btn_init(&sh, cx + IW - 360 - 14 - 230, y, 230, 64, 22, 16, 16, BTN_GHOST, "SHUFFLE CPUS", "tshuffle");
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
static void tsetup_key(int e) {
  if (!TT) return;
  int n = ts_rows(), r = ts_focus;
  if (!kbd_nav && (e == WC_KEY_UP || e == WC_KEY_DOWN || e == WC_KEY_LEFT || e == WC_KEY_RIGHT)) { kbd_nav = true; sfx_ui(); tsetup_paint(); return; }
  kbd_nav = true;
  if (e == WC_KEY_UP) { ts_focus = (r + n - 1) % n; sfx_ui(); }
  else if (e == WC_KEY_DOWN) { ts_focus = (r + 1) % n; sfx_ui(); }
  else if (e == WC_KEY_LEFT || e == WC_KEY_RIGHT) {
    int d = e == WC_KEY_RIGHT ? 1 : -1;
    if (r == 0) tourney_set_size(TT, TT->size == 4 ? 8 : 4);
    else if (r == 1) { TT->stage += d; if (TT->stage < -1) TT->stage = STAGE_N - 1; if (TT->stage >= STAGE_N) TT->stage = -1; }
    else if (r < n - 1) tourney_cycle_fighter(TT, r - 2, d);
    else ts_btn ^= 1;
    sfx_ui();
  } else if (e == WC_KEY_TAB) { if (r >= 2 && r < n - 1) { tourney_cycle_weapon(TT, r - 2, 1); sfx_ui(); } }
  else if (e == WC_KEY_ENTER || e == WC_KEY_KP_ENTER || e == WC_KEY_SPACE) {
    if (r >= 2 && r < n - 1) { tourney_cycle_ctl(TT, r - 2, 1); sfx_ui(); }
    else if (r == n - 1) { tsetup_act(ts_btn ? "tstart" : "tshuffle"); return; }
    else { ts_focus = n - 1; ts_btn = 1; sfx_ui(); }
  } else if (e == WC_KEY_ESCAPE) { act("back"); return; }
  else return;
  if (ts_focus >= ts_rows()) ts_focus = ts_rows() - 1;
  tsetup_paint();
}

/* ================================================================== the bracket */
static rlayer br_R; static int br_btn;
static int br_buttons(const char **acts, const char **labels) {
  int n = 0;
  if (!TT || TT->cur < 0) return 0;
  bool cpu = tourney_cpu_only(TT, TT->cur);
  acts[n] = "tplay"; labels[n++] = cpu ? "WATCH" : "FIGHT!";
  if (cpu) { acts[n] = "tskip"; labels[n++] = "SKIP"; }
  acts[n] = "tquit"; labels[n++] = "QUIT";
  return n;
}
static void name_box(cv *c, const tourney_t *t, int ent, float x, float y, float w, int wins, int state) {
  /* state: 0 waiting, 1 won, 2 lost */
  c_rect(c, x, y, w, 30, state == 1 ? COL(C_PAPER) : state == 2 ? COLA(C_PAPER, 0.55f) : COL(C_PAPER));
  c_border(c, x, y, w, 30, 2, COL(C_INK));
  if (ent < 0) { t_draw(c, F_TYPE, 12, 1.2f, x + 10, y + 8 + t_asc(F_TYPE, 12), "TBD", COLA(C_INK, 0.5f)); return; }
  const roster_entry *R = roster_get(t->e[ent].fighter);
  c_rect(c, x, y, 6, 30, COL(R->color));
  char tag[8]; entrant_tag(t, ent, tag, sizeof tag);
  col_t ink = state == 2 ? COLA(C_INK, 0.45f) : COL(C_INK);
  t_draw(c, F_LCD, 12, 0, x + 12, y + 8 + t_asc(F_LCD, 12), tag, ink);
  float fs = fit_fs(R->name, 15, w - 80, 10);
  t_draw(c, F_MARKER, fs, 0, x + 46, y + 7 + t_asc(F_MARKER, 15), R->name, ink);
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
    lab_paint(c, cx, by + 8, l1);
    float fs = fit_fs(l2, 24, IW - 540, 13);
    t_draw(c, F_MARKER, fs, 0, cx, by + 30 + t_asc(F_MARKER, 24), l2, COL(C_INK));
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
  if (!strcmp(id, "s-stage")) st_focus = 0;
  if (!strcmp(id, "s-tsetup")) { ts_focus = TT ? ts_rows() - 1 : 0; ts_btn = 1; }
  if (!strcmp(id, "s-bracket")) br_btn = 0;
  if (!strcmp(id, "s-champ")) { ch_idx = 0; for (int i = 0; i < 2; i++) { ch_b[i].sel = i == 0; tr_jump(&ch_b[i].t, i == 0 ? 1 : 0); ch_b[i].painted = -1; } }
  repaint();
}
void menus_draw(void) {
  const char *s = scr_cur;
  if (!strcmp(s, "s-select")) { draw_shade_v(); draw_back(); head_draw(&HD_SEL); select_draw(); return; }
  if (!strcmp(s, "s-stage")) { draw_shade_v(); draw_back(); head_draw(&HD_STAGE); R_draw(&st_R); return; }
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

static void click(const hit_t *h) {
  const char *s = scr_cur;
  switch (h->kind) {
    case H_ACT:
      if (!strcmp(s, "s-tsetup")) tsetup_act(h->s); else act(h->s);
      return;
    case H_STAGE: if (h->a >= 100) stage_pick(h->a - 100, true); else stage_pick(h->a, false); st_focus = 0; break;
    case H_TSIZE: tourney_set_size(TT, h->a); sfx_ui(); break;
    case H_TSTAGE: TT->stage += h->a; if (TT->stage < -1) TT->stage = STAGE_N - 1; if (TT->stage >= STAGE_N) TT->stage = -1; sfx_ui(); break;
    case H_TCTL: tourney_cycle_ctl(TT, h->a, 1); ts_focus = 2 + h->a; sfx_ui(); break;
    case H_TFIGHTER: tourney_cycle_fighter(TT, h->a, h->b); ts_focus = 2 + h->a; sfx_ui(); break;
    case H_TWEAPON: tourney_cycle_weapon(TT, h->a, 1); ts_focus = 2 + h->a; sfx_ui(); break;
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
  if (!pressed) return;
  if ((!strcmp(s, "s-stage") || !strcmp(s, "s-tsetup")) && btn_hit(&back_b, x, y)) {
    act(!strcmp(s, "s-stage") ? "stageback" : "back"); return;
  }
  for (int i = 0; i < nhits; i++) if (x >= hits[i].x && x < hits[i].x + hits[i].w && y >= hits[i].y && y < hits[i].y + hits[i].h) { hit_t h = hits[i]; click(&h); return; }
}

/* ui_screens.c: the page's #screens: load, title, select, controls, pause,
 * end, online and phone pads, with Rr.onKey, the click and mouseover
 * handlers, the .btn transitions and the screens' layout.
 *
 * Static content (the title's kicker, logo, tag and credits; the controls
 * card; the pause card) comes from tools/gen-ui-static.py: Chrome's own
 * layout of the page. The rest is laid out here from the page's CSS with the
 * same rules (margins collapse, flex rows centre, line boxes as in ui_int.h);
 * the numbers in comments are Chrome's for the same content. */
#include "ui_int.h"
#include "ui_kit.h"
#include "weapons.h"
#include "roster.h"
#include "stage.h"
#include "ck.h"
#include "audio.h"
#include "input.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

typedef struct { int kind, font; float fs, ls, x, y, w, h; uint32_t rgb; float a; float m[6]; float op; const char *text; } uiop;
enum { OP_RECT, OP_DASH, OP_TEXT, OP_MARK };
#include "ui_static_title.h"
#include "ui_static_pause.h"
#include "ui_static_controls.h"

const char *scr_cur;
ck_sel *scr_sel; int scr_mode = 1;
int scr_menu_idx;
/* :focus-visible: the online and pads screens' focus ring shows once the keyboard moved the focus */
bool kbd_nav;

void R_begin(rlayer *r, int x, int y, int w, int h) {
  L_ensure(&r->L, w, h); r->ox = x; r->oy = y;
  cv_set_transform(r->L.c, 1, 0, 0, 1, (float)-x, (float)-y);
}
void R_draw(rlayer *r) { if (!r->L.c) return; L_upload(&r->L); ov_draw(r->L.t, (float)r->ox, (float)r->oy, 1); }

static void draw_ops(cv *c, const uiop *ops, int n, float ox, float oy) {
  for (int i = 0; i < n; i++) {
    const uiop *o = &ops[i];
    cv_set_transform(c, o->m[0], o->m[1], o->m[2], o->m[3], o->m[4] - ox, o->m[5] - oy);
    col_t col = COLA(o->rgb, o->a * o->op);
    switch (o->kind) {
      case OP_RECT: c_rect(c, o->x, o->y, o->w, o->h, col); break;
      case OP_DASH:
        if (o->w >= o->h) c_dash_h(c, o->x, o->y, o->w, o->h, col);
        else { /* a vertical dashed edge: none in these screens */ c_rect(c, o->x, o->y, o->w, o->h, col); }
        break;
      case OP_TEXT: t_draw(c, o->font, o->fs, o->ls, o->x, o->y + t_asc(o->font, o->fs), o->text, col); break;
      case OP_MARK: cv_draw_image(c, ui_mark_px, MARK_N, MARK_N, o->x, o->y, o->w, o->h); break;
    }
  }
  cv_reset_transform(c);
}

/* ================================================================== buttons */

/* a .btn: normal (ink, cream text, 6 6 pink), .sel / :hover (pink, ink text,
 * translate(-3,-3), 9 9 ink); .ghost (cream, ink, 5 5 ink; selected: yellow,
 * translate(-3,-3), the shadow stays 5 5 ink); .go (pink, ink, 7 7 ink; selected:
 * translate(-3,-3)). transition: transform, box-shadow, background, color .08s */
static void btn_style(const btn_t *b, float s, col_t *bg, col_t *fg, float *sh, col_t *sc, float *tr) {
  if (b->kind == BTN_GHOST) {
    *bg = col_mix(COL(C_CREAM), COL(C_PINK), s); *fg = COL(C_INK); *sh = 5; *sc = COL(C_INK);
  } else if (b->kind == BTN_GO) {
    *bg = COL(C_PINK); *fg = COL(C_INK); *sh = 7; *sc = COL(C_INK);
  } else {
    *bg = col_mix(COL(C_INK), COL(C_PINK), s); *fg = col_mix(COL(C_CREAM), COL(C_INK), s);
    *sh = 6 + 3 * s; *sc = col_mix(COL(C_PINK), COL(C_INK), s);
  }
  *tr = -3 * s;
}
void btn_init(btn_t *b, float x, float y, float w, float h, float fs, float padx, float pady, int kind, const char *label, const char *act) {
  memset(b, 0, sizeof *b);
  b->x = x; b->y = y; b->w = w; b->h = h; b->fs = fs; b->padx = padx; b->pady = pady; b->kind = kind;
  b->center = kind == BTN_GO;
  snprintf(b->label, sizeof b->label, "%s", label); snprintf(b->act, sizeof b->act, "%s", act);
  tr_jump(&b->t, 0); b->painted = -1;
}
void btn_set_sel(btn_t *b, bool sel) {
  if (sel == b->sel) return;
  b->sel = sel; tr_set(&b->t, sel ? 1 : 0, ui_now, 0.08f, 0);
}
/* paint into c at the button's place (for screens painted whole) */
void btn_paint_at(cv *c, const btn_t *b, float s, float ox, float oy) {
  col_t bg, fg, sc; float sh, tr;
  btn_style(b, s, &bg, &fg, &sh, &sc, &tr);
  float x = b->x + tr - ox, y = b->y + tr - oy;
  c_box(c, x, y, b->w, b->h, bg, 4, COL(C_INK), sh, sh, sc);
  float ls = b->fs * 0.02f;
  float tx = x + 4 + b->padx;
  if (b->center) tx = x + (b->w - t_width(F_MARKER, b->fs, ls, b->label)) / 2;
  t_draw(c, F_MARKER, b->fs, ls, tx, y + 4 + b->pady + t_asc(F_MARKER, b->fs), b->label, fg);
}
void btn_draw(btn_t *b) {
  float s = tr_get(&b->t, ui_now);
  if (s != b->painted) {
    L_ensure(&b->L, (int)ceilf(b->w) + 20, (int)ceilf(b->h) + 20);
    btn_paint_at(b->L.c, b, s, b->x - 6, b->y - 6);
    b->painted = s;
  }
  L_upload(&b->L);
  ov_draw(b->L.t, b->x - 6, b->y - 6, 1);
}
bool btn_hit(const btn_t *b, float x, float y) { return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h; }

/* ================================================================== hit rects */

hit_t hits[64]; int nhits;
void hit_add(float x, float y, float w, float h, int kind, int a, int b, const char *s) {
  if (nhits >= 64) return;
  hit_t *t = &hits[nhits++]; t->x = x; t->y = y; t->w = w; t->h = h; t->kind = kind; t->a = a; t->b = b;
  snprintf(t->s, sizeof t->s, "%s", s ? s : "");
}

/* ================================================================== shared pieces */

static layer shade_h, shade_v;   /* the title's 90deg gradient, the select's 0deg one (strips) */
static void shades_init(void) {
  L_ensure(&shade_h, CK_W, 1);
  cv_fill_linear(shade_h.c, 0, 0, CK_W, 0);
  cv_fill_stop(shade_h.c, 0, "rgba(10,10,10,0.82)"); cv_fill_stop(shade_h.c, 0.55f, "rgba(10,10,10,0.35)"); cv_fill_stop(shade_h.c, 1, "rgba(10,10,10,0)");
  cv_fill_rect(shade_h.c, 0, 0, CK_W, 1);
  L_ensure(&shade_v, 1, CK_H);
  cv_fill_linear(shade_v.c, 0, CK_H, 0, 0);
  cv_fill_stop(shade_v.c, 0, "rgba(10,10,10,0.85)"); cv_fill_stop(shade_v.c, 0.6f, "rgba(10,10,10,0)");
  cv_fill_rect(shade_v.c, 0, 0, 1, CK_H);
}
void draw_shade_h(void) { L_upload(&shade_h); float m[6] = {1, 0, 0, CK_H, 0, 0}; ov_draw_m(shade_h.t, m, 1, 1); }
void draw_shade_v(void) { L_upload(&shade_v); float m[6] = {CK_W, 0, 0, 1, 0, 0}; ov_draw_m(shade_v.t, m, 1, 1); }

/* .chip: type 14, ls .16em, border 3, padding 5 10, paper / .on ink with cream text */
float chip_w(const char *s) { return t_width(F_TYPE, 14, 2.24f, s) + 26; }
void chip_paint(cv *c, float x, float y, const char *s, bool on) {
  float w = chip_w(s);
  c_box(c, x, y, w, 34, on ? COL(C_INK) : COL(C_PAPER), 3, COL(C_INK), 0, 0, COL(0));
  t_draw(c, F_TYPE, 14, 2.24f, x + 13, t_base(F_TYPE, 14, y + 8, 18), s, on ? COL(C_CREAM) : COL(C_INK));
}
/* .lab: type 13, ls .2em */
void lab_paint(cv *c, float x, float top, const char *s) { t_draw(c, F_TYPE, 13, 2.6f, x, top + t_asc(F_TYPE, 13), s, COL(C_INK)); }
void focus_ring(cv *c, float x, float y, float w, float h) {   /* outline: 3px dashed pink, offset 2 */
  c_dash_rect(c, x - 5, y - 5, w + 10, h + 10, 3, COL(C_PINK));
}

/* ================================================================== load */
static struct { trans_t bar; char msg[96]; bool err; layer msgL; float msg_w; layer ld; bool vs; layer vsL; } LD;

/* the card while a match's stage loads: the two fighters face to face, their weapons, the stage */
void ui_load_vs(const char *stage, const char *tag0, const char *tag1) {
  LD.vs = true;
  L_ensure(&LD.vsL, CK_W, 480);
  cv *c = LD.vsL.c;
  static const tshadow HS = {5, {{5, 5, COL(C_PINK)}, {-2, -2, COL(C_INK)}, {2, -2, COL(C_INK)}, {-2, 2, COL(C_INK)}, {2, 2, COL(C_INK)}}};
  float sw = t_width(F_TYPE, 16, 5.1f, stage);
  t_draw(c, F_TYPE, 16, 5.1f, 640 - sw / 2, 40, stage, COL(C_PINK));
  for (int g = 0; g < 2 && scr_sel; g++) {
    int f = roster_index(scr_sel->slots[g].ch); if (f < 0) f = g;
    int w = weapon_index(scr_sel->slots[g].weapon); if (w < 0) w = 0;
    const roster_entry *e = roster_get(f);
    uint32_t col = g ? C_CYAN : C_PINK;
    float cx = g ? 920 : 360, x = cx - 110;
    c_box(c, x, 80, 220, 220, COL(C_PAPER), 4, COL(C_INK), 8, 8, COL(col));
    select_face(c, f, x + 4, 84, 212, 212);
    c_rect(c, x + 4, 290, 212, 6, COL(e->color));
    const char *tg = g ? tag1 : tag0;
    float tw = t_width(F_LCD, 20, 0, tg) + 16;
    c_box(c, x, 54, tw, 30, COL(col), 3, COL(C_INK), 0, 0, COL(0));
    t_draw(c, F_LCD, 20, 0, x + 8, 76, tg, COL(C_INK));
    float fs = 44; while (fs > 26 && t_width(F_MARKER, fs, 0.4f, e->name) > 420) fs -= 1;
    float nw = t_width(F_MARKER, fs, 0.4f, e->name);
    tshadow sh = {5, {{5, 5, COL(col)}, {-2, -2, COL(C_INK)}, {2, -2, COL(C_INK)}, {-2, 2, COL(C_INK)}, {2, 2, COL(C_INK)}}};
    t_draw_sh(c, F_MARKER, fs, 0.4f, cx - nw / 2, 362, e->name, COL(C_CREAM), &sh);
    const char *wn = WEAPONS[w].name;
    float ww = t_width(F_TYPE, 16, 1.6f, wn);
    t_draw(c, F_TYPE, 16, 1.6f, cx - ww / 2, 396, wn, COL(C_CREAM));
  }
  float vw = t_width(F_MARKER, 96, 0, "VS");
  t_draw_sh(c, F_MARKER, 96, 0, 640 - vw / 2, 240, "VS", COL(C_CREAM), &HS);
}

static void load_paint_msg(void) {
  float w = t_width(F_TYPE, 14, 2.8f, LD.msg);
  LD.msg_w = w;
  L_ensure(&LD.msgL, (int)ceilf(w) + 4, 20);
  t_draw(LD.msgL.c, F_TYPE, 14, 2.8f, 0, t_asc(F_TYPE, 14), LD.msg, LD.err ? COL(C_ERR) : COLA(0xf5f0e6, 0.75f));
}
void scr_load(float p, const char *msg, bool err) {
  if (p >= 0) tr_set(&LD.bar, roundf(p * 100), ui_now, 0.2f, 0);
  if (msg) { t_collapse(LD.msg, msg, sizeof LD.msg); LD.err = err || LD.err; load_paint_msg(); }
}
static void load_draw(void) {
  ov_fill(0, 0, CK_W, CK_H, C_VOID, 1);
  if (LD.vs) {   /* a match's stage loading: the fighters, then the bar under them */
    L_upload(&LD.vsL); ov_draw(LD.vsL.t, 0, 60, 1);
    ov_fill(380, 518, 520, 4, C_CREAM, 1); ov_fill(380, 532, 520, 4, C_CREAM, 1);
    ov_fill(380, 522, 4, 10, C_CREAM, 1); ov_fill(896, 522, 4, 10, C_CREAM, 1);
    ov_fill(386, 524, 508 * tr_get(&LD.bar, ui_now) / 100.f, 6, C_PINK, 1);
    if (LD.msgL.c) { L_upload(&LD.msgL); ov_draw(LD.msgL.t, 640 - LD.msg_w / 2, 556, 1); }
    return;
  }
  if (!LD.ld.c) {   /* .ld: marker 84, shadow 6 6 pink; 423.45 wide at y 278 */
    L_ensure(&LD.ld, 440, 100);
    t_draw_sh(LD.ld.c, F_MARKER, 84, 0, 0, t_asc(F_MARKER, 84), "LOADING", COL(C_CREAM), &(tshadow){1, {{6, 6, COL(C_PINK)}}});
  }
  L_upload(&LD.ld);
  float w = t_width(F_MARKER, 84, 0, "LOADING");
  ov_draw(LD.ld.t, 640 - w / 2, 278, 1);
  /* .bar2: 520 x 18 at y 388, border 4 cream, padding 2; the bar pink, width % */
  ov_fill(380, 388, 520, 4, C_CREAM, 1); ov_fill(380, 402, 520, 4, C_CREAM, 1);
  ov_fill(380, 392, 4, 10, C_CREAM, 1); ov_fill(896, 392, 4, 10, C_CREAM, 1);
  float pct = tr_get(&LD.bar, ui_now);
  ov_fill(386, 394, 508 * pct / 100.f, 6, C_PINK, 1);
  if (LD.msgL.c) { L_upload(&LD.msgL); ov_draw(LD.msgL.t, 640 - LD.msg_w / 2, 424, 1); }
}

/* ================================================================== title */
static layer title_L;
static btn_t title_b[6];
static void title_init(void) {
  /* the menu, after .wrap's translateY(-52%) (-277.55) */
  float y0 = 673.75f - 277.55f;
  btn_init(&title_b[0], 76.8f, y0, 430, 48, 25.92f, 16, 6, BTN_NORMAL, "1 PLAYER", "1p");
  btn_init(&title_b[1], 76.8f, y0 + 58, 430, 48, 25.92f, 16, 6, BTN_NORMAL, "2 PLAYERS", "2p");
  btn_init(&title_b[2], 76.8f, y0 + 116, 430, 48, 25.92f, 16, 6, BTN_NORMAL, "TOURNEY", "tourney");
  btn_init(&title_b[3], 76.8f, y0 + 174, 430, 48, 25.92f, 16, 6, BTN_NORMAL, "ONLINE", "online");
  btn_init(&title_b[4], 76.8f, y0 + 232, 208, 46, 24, 12, 6, BTN_GHOST, "PHONE PADS", "pads");
  btn_init(&title_b[5], 298.8f, y0 + 232, 208, 46, 24, 12, 6, BTN_GHOST, "CONTROLS", "controls");
  L_ensure(&title_L, CK_W, CK_H);
  draw_ops(title_L.c, STATIC_TITLE, sizeof STATIC_TITLE / sizeof STATIC_TITLE[0], 0, 0);
}

/* ================================================================== pause / end */
static layer pause_L; static btn_t pause_b[3];
static void pause_init(void) {
  L_ensure(&pause_L, CK_W, CK_H);
  draw_ops(pause_L.c, STATIC_PAUSE, sizeof STATIC_PAUSE / sizeof STATIC_PAUSE[0], 0, 0);
  float x = 406, y = 479.69f - 172.344f;
  btn_init(&pause_b[0], x, y, 468, 57, 32, 22, 8, BTN_NORMAL, "RESUME", "resume");
  btn_init(&pause_b[1], x, y + 73, 468, 57, 32, 22, 8, BTN_GHOST, "CONTROLS", "controls2");
  btn_init(&pause_b[2], x, y + 146, 468, 57, 32, 22, 8, BTN_GHOST, "QUIT TO MENU", "quit");
}

static rlayer end_R; static btn_t end_b[3];
static char end_name[40] = "MOHEEB"; static bool end_wins = true; static char end_sub[64] = "FLAWLESS";
static void end_layout(void) {
  /* .center-card 520 wide, padding 18 22, centred; .big marker 86 lh .95; .sub 16 ls .18em margin-top 10; menu margin-top 16 */
  char words[48]; snprintf(words, sizeof words, "%s%s", end_name, end_wins ? " WINS" : "");
  char lines[4][160];
  int nl = t_wrap(F_MARKER, 86, 0, words, 468, false, lines, 4);
  float lh = 86 * 0.95f;
  float H = 4 + 18 + nl * lh + 10 + t_line(F_TYPE, 16) + 16 + 3 * 57 + 2 * 16 + 18 + 4;
  float top = 360 - H / 2, x = 380;
  R_begin(&end_R, 376, (int)floorf(top) - 4, 534, (int)ceilf(H) + 16);
  cv *c = end_R.L.c;
  c_box(c, x, top, 520, H, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(C_PINK));
  int name_words = 0; for (const char *p = end_name; *p; p++) if (*p == ' ') name_words++;
  name_words++;
  int wi = 0;   /* colour per word: the name ink, <em>WINS</em> pink */
  for (int k = 0; k < nl; k++) {
    float w = t_width(F_MARKER, 86, 0, lines[k]);
    float lx = x + 26 + (468 - w) / 2, base = t_base(F_MARKER, 86, top + 22 + k * lh, lh);
    char *s = lines[k];
    while (*s) {
      char word[64]; int n = 0;
      while (s[n] && s[n] != ' ') n++;
      memcpy(word, s, n); word[n] = 0;
      col_t col = wi >= name_words ? COL(C_PINK) : COL(C_INK);
      t_draw(c, F_MARKER, 86, 0, lx, base, word, col);
      lx += t_width(F_MARKER, 86, 0, word);
      s += n; wi++;
      if (*s == ' ') { lx += t_width(F_MARKER, 86, 0, " "); s++; }
    }
  }
  float sy = top + 22 + nl * lh + 10;
  float sw = t_width(F_TYPE, 16, 2.88f, end_sub);
  t_draw(c, F_TYPE, 16, 2.88f, x + 26 + (468 - sw) / 2, sy + t_asc(F_TYPE, 16), end_sub, COL(C_INK));
  float by = sy + t_line(F_TYPE, 16) + 16;
  for (int i = 0; i < 3; i++) {
    static const char *lab[3] = {"REMATCH", "CHANGE FIGHTERS", "MAIN MENU"}, *act[3] = {"rematch", "again", "quit"};
    bool keep = end_b[i].sel; trans_t t = end_b[i].t; layer L = end_b[i].L;
    btn_init(&end_b[i], 406, by + i * 73, 468, 57, 32, 22, 8, i == 0 ? BTN_NORMAL : BTN_GHOST, lab[i], act[i]);
    end_b[i].sel = keep; end_b[i].t = t; end_b[i].L = L;
  }
}
void scr_end_changed(const char *name, bool wins, const char *sub) {
  snprintf(end_name, sizeof end_name, "%s", name ? name : ""); end_wins = wins;
  snprintf(end_sub, sizeof end_sub, "%s", sub ? sub : "");
  end_layout();
}

/* ================================================================== controls */
static layer controls_L;

/* ================================================================== back button */
btn_t back_b;   /* .btn.ghost.back at (20, 20): BACK, marker 32, padding 8 22 6 */

/* ================================================================== online */
static rlayer ol_R;
static struct { float x, y, w, h; int field; } ol_fields[3];   /* name, room, link input rects */
/* the keyboard focus ring over the screen's controls (the page has none: a cabinet needs one) */
static int ol_focus;          /* index into ol_ctl */
static hit_t ol_ctl[40]; static int nol_ctl;

static void input_paint(cv *c, float x, float y, float w, const char *v, bool focus, float *caret_x) {
  /* input: type 18 600, ls .12em, uppercase, border 3, padding 6 10, paper; :focus outline 3px dashed pink, offset 2 */
  c_box(c, x, y, w, 41, COL(C_PAPER), 3, COL(C_INK), 0, 0, COL(0));
  char up[200]; int n = 0; for (const char *p = v; *p && n < 199; p++) up[n++] = (char)toupper((unsigned char)*p); up[n] = 0;
  float cw = w - 26, tw = t_width(F_TYPE, 18, 2.16f, up);
  float scroll = (focus && tw > cw) ? tw - cw : 0;
  cv_save(c);
  cv_begin(c); cv_rect(c, x + 13, y + 3, cw, 35); cv_clip(c);
  t_draw(c, F_TYPE, 18, 2.16f, x + 13 - scroll, t_base(F_TYPE, 18, y + 9, 23), up, COL(C_INK));
  cv_restore(c);
  if (caret_x) *caret_x = x + 13 + tw - scroll - (n ? 2.16f : 0);
  if (focus) c_dash_rect(c, x - 5, y - 5, w + 10, 51, 3, COL(C_PINK));
}

static float ol_caret_x, ol_caret_y; static bool ol_caret;
static void online_paint(void) {
  ui_online_t *o = &ui_online;
  nol_ctl = 0; nhits = 0;
  /* the vertical stack: h2 41 (mb 10), rows 41 / 41 / 34 (m 8), olstat (m 10), linkrow 41 (m 8), ollist (mt 6), olch 60 (mt 12) */
  char st[4][160]; int ns = t_wrap(F_TYPE, 13, 1.82f, o->status, 628, false, st, 4);
  float sth = fmaxf(18, ns * 17.f);
  bool nobody = o->link_row && o->npeers == 0;
  float listh = 0;
  if (nobody) listh = 18 + 20;   /* an .olstat inside the list: its margins 10 0 stay inside the flex box */
  for (int i = 0; i < o->npeers; i++) listh += (o->peers[i].busy ? 46 : 56) + (i ? 8 : 0);
  float y = 0;
  float y_h2 = y; y += 41;
  float y_r1 = y + 10; y = y_r1 + 41;
  float y_r2 = y + 8; y = y_r2 + 41;
  float y_r3 = y + 8; y = y_r3 + 34;
  float y_r4 = y + 8; y = y_r4 + 34;
  float y_r5 = y + 8; y = y_r5 + 34;
  float y_nt = y + 6; y = y_nt + 14;
  float y_st = y + 10; y = y_st + sth;
  float y_lk = 0; float mb = 10;
  if (o->link_row) { y_lk = y + 10; y = y_lk + 41; mb = 8; }
  float y_ls = y + fmaxf(mb, 6); y = y_ls + listh;
  float y_ch = 0; if (o->challenge) { y_ch = y + 12; y = y_ch + 60; }
  float H = 4 + 18 + y + 18 + 4;
  float top = 360 - H / 2, x0 = 300, cx = 326, cy = top + 22;
  R_begin(&ol_R, 290, (int)floorf(top) - 10, 710, (int)ceilf(H) + 24);
  cv *c = ol_R.L.c;
  c_box(c, x0, top, 680, H, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(C_PINK));
  /* h2 */
  t_draw(c, F_MARKER, 30, 0, cx, t_base(F_MARKER, 30, cy + y_h2, 30), "ONLINE VERSUS", COL(C_INK));
  c_rect(c, cx, cy + y_h2 + 38, 628, 3, COL(C_INK));
  /* NAME */
  float lw = t_width(F_TYPE, 13, 2.6f, "NAME");
  lab_paint(c, cx, cy + y_r1 + 12, "NAME");
  float fx = cx + lw + 10;
  input_paint(c, fx, cy + y_r1, 628 - lw - 10, o->name, o->focus == 1, o->focus == 1 ? &ol_caret_x : NULL);
  if (o->focus == 1) ol_caret_y = cy + y_r1 + 9;
  ol_fields[0].x = fx; ol_fields[0].y = cy + y_r1; ol_fields[0].w = 628 - lw - 10; ol_fields[0].h = 41;
  ol_ctl[nol_ctl++] = (hit_t){fx, cy + y_r1, 628 - lw - 10, 41, H_FIELD, 1, 0, ""};
  /* ROOM + MAIN LOBBY / NEW ROOM / JOIN */
  lab_paint(c, cx, cy + y_r2 + 12, "ROOM");
  float w1 = chip_w("MAIN LOBBY"), w2 = chip_w("NEW ROOM"), w3 = chip_w("JOIN");
  float rw = 628 - lw - w1 - w2 - w3 - 40;
  input_paint(c, fx, cy + y_r2, rw, o->room, o->focus == 2, o->focus == 2 ? &ol_caret_x : NULL);
  if (o->focus == 2) ol_caret_y = cy + y_r2 + 9;
  ol_ctl[nol_ctl++] = (hit_t){fx, cy + y_r2, rw, 41, H_FIELD, 2, 0, ""};
  float bx = fx + rw + 10, by = cy + y_r2 + 3.5f;
  chip_paint(c, bx, by, "MAIN LOBBY", false); ol_ctl[nol_ctl++] = (hit_t){bx, by, w1, 34, H_ACT, 0, 0, "ollobby"}; bx += w1 + 10;
  chip_paint(c, bx, by, "NEW ROOM", false); ol_ctl[nol_ctl++] = (hit_t){bx, by, w2, 34, H_ACT, 0, 0, "olnew"}; bx += w2 + 10;
  chip_paint(c, bx, by, "JOIN", true); ol_ctl[nol_ctl++] = (hit_t){bx, by, w3, 34, H_ACT, 0, 0, "oljoin"};
  /* FIGHTER, WEAPON, STAGE: a value between two chips that step it (the web page only sees the
   * page's own: MOHEEB or LUIS, its four weapons, the workshop) */
  {
    int fi = roster_index(o->fighter), wi = weapon_index(o->weapon), si = stage_index(o->stage);
    const char *val[3] = {fi >= 0 ? ROSTER[fi].name : "?", wi >= 0 ? WEAPONS[wi].name : "?", si >= 0 ? STAGES[si].name : "?"};
    const char *lab[3] = {"FIGHTER", "WEAPON", "STAGE"};
    float ry[3] = {y_r3, y_r4, y_r5}; int kind[3] = {H_OLF, H_OLW, H_OLS};
    for (int k = 0; k < 3; k++) {
      lab_paint(c, cx, cy + ry[k] + 8.5f, lab[k]);
      float x = cx + 100;
      chip_paint(c, x, cy + ry[k], "<", false); ol_ctl[nol_ctl++] = (hit_t){x, cy + ry[k], chip_w("<"), 34, kind[k], -1, 0, ""};
      x += chip_w("<") + 10;
      t_draw(c, F_MARKER, 20, 0, x, cy + ry[k] + 5 + t_asc(F_MARKER, 20), val[k], COL(C_INK));
      x += t_width(F_MARKER, 20, 0, val[k]) + 10;
      chip_paint(c, x, cy + ry[k], ">", false); ol_ctl[nol_ctl++] = (hit_t){x, cy + ry[k], chip_w(">"), 34, kind[k], 1, 0, ""};
    }
    const char *note = "VERSUS THE WEB VERSION: MOHEEB AND LUIS IN THE WORKSHOP";
    t_draw(c, F_TYPE, 11, 1.1f, cx, cy + y_nt + t_asc(F_TYPE, 11), note, COLA(C_INK, 0.6f));
  }
  /* status */
  for (int k = 0; k < ns; k++) t_draw(c, F_TYPE, 13, 1.82f, cx, cy + y_st + k * 17 + t_asc(F_TYPE, 13), st[k], COL(C_INK));
  /* LINK + COPY LINK */
  if (o->link_row) {
    lab_paint(c, cx, cy + y_lk + 12, "LINK");
    float wc = chip_w("COPY LINK"), iw = 628 - lw - wc - 20;
    input_paint(c, fx, cy + y_lk, iw, o->link, false, NULL);
    chip_paint(c, fx + iw + 10, cy + y_lk + 3.5f, "COPY LINK", true);
    ol_ctl[nol_ctl++] = (hit_t){fx + iw + 10, cy + y_lk + 3.5f, wc, 34, H_ACT, 0, 0, "olcopy"};
  }
  /* the peers */
  float py = cy + y_ls;
  if (nobody) t_draw(c, F_TYPE, 13, 1.82f, cx, py + 10 + t_asc(F_TYPE, 13), "NOBODY ELSE HERE YET.", COL(C_INK));
  for (int i = 0; i < o->npeers; i++) {
    float h = o->peers[i].busy ? 46 : 56;
    c_box(c, cx, py, 628, h, COL(C_PAPER), 3, COL(C_INK), 0, 0, COL(0));
    float mid = py + h / 2;
    t_draw(c, F_MARKER, 22, 0, cx + 15, mid - 12 + t_asc(F_MARKER, 22), o->peers[i].n, COL(C_INK));
    char wp[64]; int wk = weapon_index(o->peers[i].w), fk = roster_index(o->peers[i].c);
    const char *wn = wk >= 0 ? WEAPONS[wk].name : "?";
    if (fk >= 0) snprintf(wp, sizeof wp, "%s, %s%s", ROSTER[fk].name, wn, o->peers[i].busy ? " IN A FIGHT" : "");
    else snprintf(wp, sizeof wp, "%s%s", wn, o->peers[i].busy ? " IN A FIGHT" : "");
    float pw = t_width(F_TYPE, 12, 1.68f, wp);
    float right = cx + 628 - 15;
    if (!o->peers[i].busy) {
      float wc = chip_w("CHALLENGE");
      chip_paint(c, right - wc, mid - 17, "CHALLENGE", true);
      ol_ctl[nol_ctl++] = (hit_t){right - wc, mid - 17, wc, 34, H_CH, i, 0, ""};
      right -= wc + 10;
    }
    t_draw(c, F_TYPE, 12, 1.68f, right - pw, mid - 7.5f + t_asc(F_TYPE, 12), wp, COLA(C_INK, 0.7f));
    py += h + 8;
  }
  /* the challenge banner */
  if (o->challenge) {
    float by2 = cy + y_ch;
    c_box(c, cx, by2, 628, 60, COL(C_PINK), 3, COL(C_INK), 0, 0, COL(0));
    float tw = t_width(F_MARKER, 20, 0, o->chtxt);
    t_draw(c, F_MARKER, 20, 0, cx + 13, by2 + 19 + t_asc(F_MARKER, 20), o->chtxt, COL(C_INK));
    float ax = cx + 13 + tw + 10;
    chip_paint(c, ax, by2 + 13, "ACCEPT", true); ol_ctl[nol_ctl++] = (hit_t){ax, by2 + 13, chip_w("ACCEPT"), 34, H_ACT, 0, 0, "olaccept"};
    ax += chip_w("ACCEPT") + 10;
    chip_paint(c, ax, by2 + 13, "DECLINE", false); ol_ctl[nol_ctl++] = (hit_t){ax, by2 + 13, chip_w("DECLINE"), 34, H_ACT, 0, 0, "oldecline"};
  }
  ol_ctl[nol_ctl++] = (hit_t){back_b.x, back_b.y, back_b.w, back_b.h, H_ACT, 0, 0, "onback"};
  if (ol_focus >= nol_ctl) ol_focus = nol_ctl - 1;
  /* the keyboard focus on a control that is not a field: the same dashed ring */
  hit_t *f = &ol_ctl[ol_focus];
  if (kbd_nav && f->kind != H_FIELD && strcmp(f->s, "onback")) focus_ring(c, f->x, f->y, f->w, f->h);
  ol_caret = o->focus != 0;
}
void scr_online_changed(void) { if (scr_cur && !strcmp(scr_cur, "s-online")) online_paint(); }

/* ================================================================== pads */
static rlayer pd_R; static hit_t pd_ctl[2]; static int pd_focus;
static void pads_paint(void) {
  ui_pads_t *p = &ui_pads;
  /* .pdinfo: max-width 360; the code (marker 56, ls .08em, lh 1), the url and status (lcd 16, break-all) */
  char ul[6][160], sl[6][160], url[160], stt[160];
  t_collapse(url, p->url, sizeof url); t_collapse(stt, p->st, sizeof stt);   /* white-space: normal */
  int nu = t_wrap(F_LCD, 16, 0, url, 360, true, ul, 6), nsl = t_wrap(F_LCD, 16, 0, stt, 360, true, sl, 6);
  float info_h = 17 + 56 + 17 + nu * 21 + 6 + 34 + 8 + 17 + nsl * 21;
  float row_h = fmaxf(180, info_h);
  float H = 4 + 18 + 41 + 10 + 34 + 10 + row_h + 18 + 4;
  float top = 360 - H / 2, cx = 326, cy = top + 22;
  R_begin(&pd_R, 290, (int)floorf(top) - 10, 710, (int)ceilf(H) + 24);
  cv *c = pd_R.L.c;
  c_box(c, 300, top, 680, H, COL(C_CREAM), 4, COL(C_INK), 8, 8, COL(C_PINK));
  t_draw(c, F_MARKER, 30, 0, cx, t_base(F_MARKER, 30, cy, 30), "PHONE PADS", COL(C_INK));
  c_rect(c, cx, cy + 38, 628, 3, COL(C_INK));
  /* #pd-help, as Chrome wraps it */
  t_draw(c, F_TYPE, 13, 1.82f, cx, cy + 51 + t_asc(F_TYPE, 13), "Open the link on a phone and it becomes a controller instead of", COL(C_INK));
  t_draw(c, F_TYPE, 13, 1.82f, cx, cy + 68 + t_asc(F_TYPE, 13), "the game. First pad is P1, add &p=2 for P2.", COL(C_INK));
  float ry = cy + 95;
  /* #pd-qr: 180 square, white, border 4, padding 6; the svg's viewBox -2 -2 n+4 n+4, crisp edges */
  float qy = ry + (row_h - 180) / 2;
  c_box(c, cx, qy, 180, 180, COL(0xffffff), 4, COL(C_INK), 0, 0, COL(0));
  if (p->qr_n > 0) {
    float m = 160.f / (p->qr_n + 4), ox = cx + 10 + 2 * m, oy = qy + 10 + 2 * m;
    for (int yy = 0; yy < p->qr_n; yy++) for (int xx = 0; xx < p->qr_n; xx++) if (p->qr[yy * p->qr_n + xx]) {
      /* crispEdges: snap the module to whole pixels */
      float x0 = roundf(ox + xx * m), y0 = roundf(oy + yy * m), x1 = roundf(ox + (xx + 1) * m), y1 = roundf(oy + (yy + 1) * m);
      c_rect(c, x0, y0, x1 - x0, y1 - y0, COL(C_INK));
    }
  }
  float ix = cx + 200, iy = ry + (row_h - info_h) / 2;
  lab_paint(c, ix, iy, "PAD CODE"); iy += 17;
  t_draw(c, F_MARKER, 56, 4.48f, ix, t_base(F_MARKER, 56, iy, 56), p->code, COL(C_INK)); iy += 56;
  lab_paint(c, ix, iy, "LINK"); iy += 17;
  for (int k = 0; k < nu; k++) t_draw(c, F_LCD, 16, 0, ix, iy + k * 21 + t_asc(F_LCD, 16), ul[k], COL(C_INK));
  iy += nu * 21 + 6;
  chip_paint(c, ix, iy, "COPY LINK", true);
  pd_ctl[0] = (hit_t){ix, iy, chip_w("COPY LINK"), 34, H_ACT, 0, 0, "padcopy"};
  pd_ctl[1] = (hit_t){back_b.x, back_b.y, back_b.w, back_b.h, H_ACT, 0, 0, "padsback"};
  if (kbd_nav && pd_focus == 0) focus_ring(c, ix, iy, chip_w("COPY LINK"), 34);
  iy += 34 + 8;
  lab_paint(c, ix, iy, "STATUS"); iy += 17;
  for (int k = 0; k < nsl; k++) t_draw(c, F_LCD, 16, 0, ix, iy + k * 21 + t_asc(F_LCD, 16), sl[k], COL(C_INK));
}
void scr_pads_changed(void) { if (scr_cur && !strcmp(scr_cur, "s-pads")) pads_paint(); }

/* ================================================================== show / frame / draw */

static btn_t *menu_btns(int *n) {
  if (!scr_cur) { *n = 0; return NULL; }
  if (!strcmp(scr_cur, "s-title")) { *n = 6; return title_b; }
  if (!strcmp(scr_cur, "s-pause")) { *n = 3; return pause_b; }
  if (!strcmp(scr_cur, "s-end")) { *n = 3; return end_b; }
  *n = 0; return NULL;
}
static void update_menu_sel(void) {
  int n; btn_t *b = menu_btns(&n);
  for (int i = 0; i < n; i++) btn_set_sel(&b[i], i == scr_menu_idx);
}

void scr_init(void) {
  shades_init();
  title_init();
  pause_init();
  L_ensure(&controls_L, CK_W, CK_H);
  draw_ops(controls_L.c, STATIC_CONTROLS, sizeof STATIC_CONTROLS / sizeof STATIC_CONTROLS[0], 0, 0);
  btn_init(&back_b, 20, 20, t_width(F_MARKER, 32, 0.64f, "BACK") + 52, 57, 32, 22, 8, BTN_GHOST, "BACK", "back");
  tr_jump(&LD.bar, 0);
  snprintf(LD.msg, sizeof LD.msg, "unpacking the hackerspace"); load_paint_msg();
  end_layout();
  menus_init();
  scr_cur = "s-load";
}

void scr_show(const char *id) {
  scr_cur = id;
  if (!id || strcmp(id, "s-load")) LD.vs = false;
  if (!id) return;
  if (!strcmp(id, "s-title") || !strcmp(id, "s-pause") || !strcmp(id, "s-end")) {
    scr_menu_idx = 0;
    int n; btn_t *b = menu_btns(&n);
    for (int i = 0; i < n; i++) { b[i].sel = i == 0; tr_jump(&b[i].t, i == 0 ? 1 : 0); b[i].painted = -1; }
  }
  kbd_nav = false;
  if (menus_owns(id)) { menus_show(id); return; }
  if (!strcmp(id, "s-online")) { ol_focus = 0; online_paint(); }
  if (!strcmp(id, "s-pads")) { pd_focus = 0; pads_paint(); }
}

void scr_frame(void) {
  /* the lobby and the pads change from the network: repaint when what they show changed */
  {
    static ui_online_t ol_seen; static ui_pads_t pd_seen;
    if (scr_cur && !strcmp(scr_cur, "s-online") && memcmp(&ol_seen, &ui_online, sizeof ol_seen)) { ol_seen = ui_online; online_paint(); }
    if (scr_cur && !strcmp(scr_cur, "s-pads") && memcmp(&pd_seen, &ui_pads, sizeof pd_seen)) { pd_seen = ui_pads; pads_paint(); }
  }
  /* the text fields: typing goes to the focused one (the page's inputs) */
  if (scr_cur && !strcmp(scr_cur, "s-online") && ui_online.focus) {
    const char *t; int n = input_text(&t);
    if (n > 0) {
      char *v = ui_online.focus == 1 ? ui_online.name : ui_online.room;
      int cap = ui_online.focus == 1 ? 14 : 16;
      for (int i = 0; i < n; i++) {
        char ch = (char)toupper((unsigned char)t[i]);
        if (ui_online.focus == 2 && !(isalnum((unsigned char)ch) || ch == '_' || ch == '-')) continue;   /* the input handler's filter */
        int l = (int)strlen(v); if (l < cap) { v[l] = ch; v[l + 1] = 0; }
      }
      online_paint();
    }
  }
}

void draw_back(void) {
  /* .screen .back: z-index 3 over the card; a ghost .btn without the selected state */
  btn_draw(&back_b);
}

void scr_draw(void) {
  if (!scr_cur) return;
  const char *s = scr_cur;
  if (!strcmp(s, "s-load")) { load_draw(); return; }
  if (!strcmp(s, "s-title")) {
    draw_shade_h();
    L_upload(&title_L); ov_draw(title_L.t, 0, 0, 1);
    for (int i = 0; i < 6; i++) btn_draw(&title_b[i]);
    return;
  }
  if (menus_owns(s)) { menus_draw(); return; }
  if (!strcmp(s, "s-controls")) { ov_fill(0, 0, CK_W, CK_H, 0x0a0a0a, 0.72f); L_upload(&controls_L); ov_draw(controls_L.t, 0, 0, 1); return; }
  if (!strcmp(s, "s-pause")) {
    ov_fill(0, 0, CK_W, CK_H, 0x0a0a0a, 0.72f);
    L_upload(&pause_L); ov_draw(pause_L.t, 0, 0, 1);
    for (int i = 0; i < 3; i++) btn_draw(&pause_b[i]);
    return;
  }
  if (!strcmp(s, "s-end")) {
    ov_fill(0, 0, CK_W, CK_H, 0x0a0a0a, 0.6f);
    R_draw(&end_R);
    for (int i = 0; i < 3; i++) btn_draw(&end_b[i]);
    return;
  }
  if (!strcmp(s, "s-online")) {
    ov_fill(0, 0, CK_W, CK_H, 0x0a0a0a, 0.72f);
    R_draw(&ol_R);
    if (ol_caret && fmodf(ui_now, 1.f) < 0.5f) ov_fill(ol_caret_x, ol_caret_y, 1, 23, C_INK, 1);
    draw_back();
    if (ol_focus == nol_ctl - 1) { /* BACK focused */ }
    return;
  }
  if (!strcmp(s, "s-pads")) {
    ov_fill(0, 0, CK_W, CK_H, 0x0a0a0a, 0.72f);
    R_draw(&pd_R);
    draw_back();
    return;
  }
}

/* ================================================================== keys and clicks */

static void act(const char *a) { sfx_ui_ok(); if (ui_cb.act) ui_cb.act(a); }

static void click_hit(const hit_t *h) {
  switch (h->kind) {
    case H_ACT: act(h->s); break;
    case H_OLW: { int w = (weapon_index(ui_online.weapon) + h->a + WEAPON_N) % WEAPON_N; if (ui_on_olweapon) ui_on_olweapon(WEAPONS[w].key); sfx_ui(); break; }
    case H_OLF: { int f = (roster_index(ui_online.fighter) + h->a + ROSTER_N) % ROSTER_N; if (ui_on_olfighter) ui_on_olfighter(ROSTER[f].key); sfx_ui(); break; }
    case H_OLS: { int g = (stage_index(ui_online.stage) + h->a + STAGE_N) % STAGE_N; if (ui_on_olstage) ui_on_olstage(STAGES[g].key); sfx_ui(); break; }
    case H_CH: if (ui_on_challenge) ui_on_challenge(ui_online.peers[h->a].id); break;
    case H_FIELD: ui_online.focus = h->a; online_paint(); break;
  }
}

static void online_key(int code) {
  ui_online_t *o = &ui_online;
  if (o->focus) {   /* a focused input takes its keys (stopPropagation in the page) */
    char *v = o->focus == 1 ? o->name : o->room;
    if (code == WC_KEY_BACKSPACE) { int l = (int)strlen(v); if (l) v[l - 1] = 0; online_paint(); return; }
    if (code == WC_KEY_ENTER || code == WC_KEY_KP_ENTER) {
      if (o->focus == 1) { o->focus = 2; ol_focus = 1; }
      else if (ui_on_room_enter) ui_on_room_enter();
      online_paint(); return;
    }
    if (code != WC_KEY_UP && code != WC_KEY_DOWN && code != WC_KEY_ESCAPE && code != WC_KEY_TAB) return;
  }
  int n = nol_ctl;
  bool nav = code == WC_KEY_UP || code == WC_KEY_DOWN || code == WC_KEY_LEFT || code == WC_KEY_RIGHT || code == WC_KEY_TAB;
  if (nav && !kbd_nav) {   /* the first arrow only shows where the focus is: the name field */
    kbd_nav = true; ol_focus = 0; o->focus = 1; sfx_ui(); online_paint(); return;
  }
  if (code == WC_KEY_UP || code == WC_KEY_LEFT || (code == WC_KEY_TAB && input_key_down(CK_KEY_LSHIFT))) ol_focus = (ol_focus + n - 1) % n;
  else if (code == WC_KEY_DOWN || code == WC_KEY_RIGHT || code == WC_KEY_TAB) ol_focus = (ol_focus + 1) % n;
  else if (code == WC_KEY_ESCAPE) { act("onback"); return; }
  else if (code == WC_KEY_ENTER || code == WC_KEY_SPACE) { hit_t h = ol_ctl[ol_focus]; click_hit(&h); if (scr_cur && !strcmp(scr_cur, "s-online")) online_paint(); return; }
  else return;
  kbd_nav = true;
  o->focus = ol_ctl[ol_focus].kind == H_FIELD ? ol_ctl[ol_focus].a : 0;
  sfx_ui();
  online_paint();
}

void scr_key(int e) {
  const char *P = scr_cur;
  if (!P) return;
  if (select_modal_active()) { select_key(e); return; }
  if (!strcmp(P, "s-online")) { online_key(e); return; }
  if (!strcmp(P, "s-pads")) {
    if (e == WC_KEY_UP || e == WC_KEY_DOWN || e == WC_KEY_LEFT || e == WC_KEY_RIGHT || e == WC_KEY_TAB) {
      if (kbd_nav) pd_focus ^= 1;
      kbd_nav = true; sfx_ui(); pads_paint();
    }
    else if (e == WC_KEY_ENTER || e == WC_KEY_SPACE) click_hit(&pd_ctl[pd_focus]);
    else if (e == WC_KEY_ESCAPE) act("padsback");
    return;
  }
  if (menus_owns(P)) { menus_key(e); return; }
  if (!strcmp(P, "s-controls")) {
    if (e == WC_KEY_ESCAPE || e == WC_KEY_ENTER || e == WC_KEY_SPACE) { sfx_ui(); if (ui_cb.act) ui_cb.act("back"); }
    return;
  }
  int n; btn_t *t = menu_btns(&n);
  if (!n) return;
  if (e == WC_KEY_UP || e == WC_KEY_W) { scr_menu_idx = (scr_menu_idx + n - 1) % n; update_menu_sel(); sfx_ui(); }
  else if (e == WC_KEY_DOWN || e == WC_KEY_S) { scr_menu_idx = (scr_menu_idx + 1) % n; update_menu_sel(); sfx_ui(); }
  else if (e == WC_KEY_ENTER || e == WC_KEY_KP_ENTER || e == WC_KEY_SPACE) {
    sfx_ui_ok();
    act(t[scr_menu_idx].act);   /* t[idx].click(): the click handler plays uiOk again */
  }
}

static bool was_down;
void scr_pointer(float x, float y, bool pressed) {
  const char *P = scr_cur;
  (void)was_down;
  if (select_modal_active()) { select_pointer(x, y, pressed); return; }
  /* mouseover: a .menu .btn under the pointer becomes the selected one */
  int n; btn_t *b = menu_btns(&n);
  for (int i = 0; i < n; i++) if (btn_hit(&b[i], x, y) && i != scr_menu_idx) { scr_menu_idx = i; update_menu_sel(); }
  if (menus_owns(P)) { menus_pointer(x, y, pressed); return; }
  if (!pressed) return;
  for (int i = 0; i < n; i++) if (btn_hit(&b[i], x, y)) { act(b[i].act); return; }
  if (!strcmp(P, "s-online") || !strcmp(P, "s-pads")) {
    if (btn_hit(&back_b, x, y)) { act(!strcmp(P, "s-online") ? "onback" : "padsback"); return; }
  }
  if (!strcmp(P, "s-controls")) {
    /* GOT IT: .rowbtns .btn at the card's bottom left (Chrome: 236, 568, 172 x 57) */
    if (x >= 236 && x < 408 && y >= 568 && y < 625) act("back");
    return;
  }
  if (!strcmp(P, "s-online")) {
    for (int i = 0; i < nol_ctl; i++) { hit_t *h = &ol_ctl[i]; if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h) { ol_focus = i; hit_t c = *h; click_hit(&c); online_paint(); return; } }
  }
  if (!strcmp(P, "s-pads")) { if (x >= pd_ctl[0].x && x < pd_ctl[0].x + pd_ctl[0].w && y >= pd_ctl[0].y && y < pd_ctl[0].y + pd_ctl[0].h) click_hit(&pd_ctl[0]); }
}

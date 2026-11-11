/* game.c: the game (wv) and the page's startup (the async main: Rr, the
 * callbacks, the act switch and the key handler). The online host's event
 * buffer and snapshots, and the guest's replay, are here too (they are wv's). */
#include "game.h"
#include "ck.h"
#include "fighter.h"
#include "arena.h"
#include "gfx.h"
#include "fx.h"
#include "ui.h"
#include "audio.h"
#include "input.h"
#include "dance.h"
#include "songs.h"
#include "online.h"
#include "overlay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* yD, Rt, GC, Dv, AI */
static const struct { const char *key, *rig, *name; } CHARS[2] = {{"moheeb", "b", "MOHEEB"}, {"luis", "a", "LUIS"}};
#define RT 2
#define GC 60.0f
#define DV 4.45f
#define AI_X 2.3f

enum { PH_LOAD, PH_MENU, PH_INTRO, PH_FIGHT, PH_KO, PH_BETWEEN, PH_MATCHEND, PH_DANCE };
static const char *PH_NAMES[] = {"load", "menu", "intro", "fight", "ko", "between", "matchend", "dance"};
static int ph_index(const char *n) { for (int i = 0; i < 8; i++) if (!strcmp(PH_NAMES[i], n)) return i; return PH_MENU; }

static struct {
  t3_scene *scene;
  t3_camera *cam;
  fighter *chars[2];            /* moheeb, luis */
  fighter *f[2];                /* this.fighters */
  bool arena_ready;
  int net_role;
  int phase; float phase_t;
  bool paused;
  int mode;
  ck_sel sel; ck_sel sel_backup; bool has_backup;
  int wins[2], round;
  float time_left, slow, freeze, shake, cam_yaw;
  struct { v3 pos, look; } cs;
  bool has_brain; brain br;
  float menu_orbit;
  int ko_winner; bool perfect;
  float t;
  bool cam_snap;
  int intro_stage;
  bool match_over;
  v2 cam_fwd;
  /* dance */
  bool dance_on; uint32_t dance_seed; int last_track; bool has_dance_res; dance_result dance_res;
  double dance_idle_t[2]; uint32_t dance_seed_done;
  /* online */
  bool has_online_names; char online_names[2][16];
  double snap_timer, int_timer; int snap_n, ev_id, last_ev;
  struct { mp_val *e; double t; } evbuf[120]; int nev;
  mp_val *snap; double snap_at;
  bool has_remote; float rem_mx, rem_my; bool rem_b; int rem_c[5], rem_seen[5];
  int guest_counts[5];
  bool rematch_asked;
  double idle_ms;
  char screen_before_controls[16];   /* D of the page's act switch */
} G;

static fighter *other(int i) { return G.f[1 - i]; }
static int char_index(const char *k) { return !strcmp(k, "luis") ? 1 : 0; }

/* ---------------------------------------------------------------- the online host's UI proxy (w3) */

static void push_ev(mp_val *e) {
  mp_set(e, "i", mp_num(++G.ev_id));
  if (G.nev == 120) { mp_free(G.evbuf[0].e); memmove(&G.evbuf[0], &G.evbuf[1], sizeof G.evbuf[0] * 119); G.nev--; }
  G.evbuf[G.nev].e = e; G.evbuf[G.nev].t = ck_now() * 1000; G.nev++;
}
static mp_val *ui_ev(const char *m) {
  mp_val *e = mp_map(); mp_set(e, "k", mp_str("ui")); mp_set(e, "m", mp_str(m));
  mp_set(e, "a", mp_arr());
  return e;
}
static void arg(mp_val *e, mp_val *v) { mp_push((mp_val *)mp_get(e, "a"), v); }
#define HOSTING (G.net_role == NET_HOST)

static void gui_announce(const char *a, const char *b) {
  ui_announce(a, b, false);
  if (HOSTING) { mp_val *e = ui_ev("announce"); arg(e, mp_str(a)); arg(e, mp_str(b)); push_ev(e); }
}
static void gui_clear_announce(void) { ui_clear_announce(); if (HOSTING) push_ev(ui_ev("clearAnnounce")); }
static void gui_set_pips(int i, int on) {
  ui_set_pips(i, on, RT);
  if (HOSTING) { mp_val *e = ui_ev("setPips"); arg(e, mp_num(i)); arg(e, mp_num(on)); arg(e, mp_num(RT)); push_ev(e); }
}
static void gui_set_round(const char *s) { ui_set_round(s); if (HOSTING) { mp_val *e = ui_ev("setRound"); arg(e, mp_str(s)); push_ev(e); } }
static void gui_combo(int i, int n, const char *s) {
  ui_combo(i, n, s);
  if (HOSTING) { mp_val *e = ui_ev("combo"); arg(e, mp_num(i)); arg(e, mp_num(n)); if (s) arg(e, mp_str(s)); push_ev(e); }
}
static void gui_flash(uint32_t c, float a, float ms) {
  ui_flash(c, a, ms);
  if (HOSTING) { char h[8]; snprintf(h, 8, "#%06x", c); mp_val *e = ui_ev("flash"); arg(e, mp_str(h)); arg(e, mp_num(a)); arg(e, mp_num(ms)); push_ev(e); }
}
static void gui_set_end(const char *name, bool wins, const char *sub) {
  ui_set_end(name, wins, sub);
  if (HOSTING) {
    char html[64]; snprintf(html, sizeof html, wins ? "%s <em>WINS</em>" : "%s", name);
    mp_val *e = ui_ev("setEnd"); arg(e, mp_str(html)); arg(e, mp_str(sub)); push_ev(e);
  }
}
static void gui_hide_all(void) { ui_hide_all(); if (HOSTING) push_ev(ui_ev("hideAll")); }
static void gui_hud(bool on) { ui_hud(on); if (HOSTING) { mp_val *e = ui_ev("hud"); arg(e, mp_bool(on)); push_ev(e); } }
static void gui_set_hint(const char *s) { ui_set_hint(s); if (HOSTING) { mp_val *e = ui_ev("setHint"); arg(e, mp_str(s)); push_ev(e); } }
static void gui_toast(const char *s) { ui_toast(s); if (HOSTING) { mp_val *e = ui_ev("toast"); arg(e, mp_str(s)); push_ev(e); } }
static void gui_show(const char *s) { ui_show(s); if (HOSTING && !strcmp(s, "s-end")) { mp_val *e = ui_ev("show"); arg(e, mp_str(s)); push_ev(e); } }
static void gui_set_names(const char *a, const char *b, const char *pa, const char *pb) {
  ui_set_names(a, b, pa, pb);
  if (HOSTING) {
    mp_val *e = ui_ev("setNames"); arg(e, mp_str(a)); arg(e, mp_str(b));
    mp_val *l = mp_arr(); mp_push(l, mp_str(pa)); mp_push(l, mp_str(pb)); arg(e, l);
    push_ev(e);
  }
}

static void sfx_tapped(const char *name, float a) {
  if (!HOSTING) return;
  mp_val *e = mp_map(); mp_set(e, "k", mp_str("sfx")); mp_set(e, "m", mp_str(name));
  mp_val *l = mp_arr(); mp_push(l, mp_num(a)); mp_set(e, "a", l);
  push_ev(e);
}

/* ---------------------------------------------------------------- setup */

static void labels(const char **a, const char **b) {
  if (G.has_online_names) { *a = G.online_names[0]; *b = G.online_names[1]; return; }
  *a = "P1"; *b = G.mode == 1 ? "CPU" : "P2";
}

static void place_fighters(void) {
  fighter *A = G.f[0], *P = G.f[1];
  fighter_reset(A); fighter_reset(P);
  t3_object_set_position(A->root, -AI_X, 0, 0); t3_object_set_position(P->root, AI_X, 0, 0);
  A->yaw = CK_PI / 2; P->yaw = -CK_PI / 2;
  t3_object_set_rotation(A->root, 0, A->yaw, 0); t3_object_set_rotation(P->root, 0, P->yaw, 0);
  trail_clear(A->trail); trail_clear(P->trail);
}

static void apply_sel(void) {
  for (int t = 0; t < 2; t++) {
    fighter *e = G.chars[char_index(G.sel.slots[t].ch)];
    G.f[t] = e;
    e->slot = t;
    fighter_set_weapon(e, weapon_index(G.sel.slots[t].weapon));
    trail_set_color(e->trail, e->W->trail);
  }
  const char *a, *b; labels(&a, &b);
  gui_set_names(CHARS[char_index(G.sel.slots[0].ch)].name, CHARS[char_index(G.sel.slots[1].ch)].name, a, b);
  place_fighters();
}

static void menu_mode(void) {
  G.phase = PH_MENU; gui_hud(false); gui_set_hint(""); music_set_level(0.35f); place_fighters();
}

static void start_round(void);
static void start_match(void) {
  G.wins[0] = G.wins[1] = 0; G.round = 1;
  G.has_brain = G.mode == 1 && !G.net_role;
  if (G.has_brain) brain_init(&G.br, G.sel.level);
  input_mode = G.mode;
  gui_hide_all(); gui_hud(true);
  const char *a, *b; labels(&a, &b);
  gui_set_names(CHARS[char_index(G.sel.slots[0].ch)].name, CHARS[char_index(G.sel.slots[1].ch)].name, a, b);
  gui_set_pips(0, 0); gui_set_pips(1, 0);
  start_round();
}

static void start_round(void) {
  place_fighters();
  for (int i = 0; i < 2; i++) { G.f[i]->hp = 100; G.f[i]->meter = fminf(G.f[i]->meter, 0); }
  G.time_left = GC; G.slow = 1; G.freeze = 0; G.shake = 0; G.ko_winner = -1; G.perfect = false;
  ui_set_health(0, 100, true); ui_set_health(1, 100, true); ui_set_meter(0, 0); ui_set_meter(1, 0);
  ui_set_timer(GC);
  char r[24]; snprintf(r, sizeof r, "ROUND %d", G.round);
  gui_set_round(r);
  gui_combo(0, 0, NULL); gui_combo(1, 0, NULL);
  gui_set_hint(G.mode == 1 ? "WASD move   Q light   E heavy   R kick   SPACE block   SHIFT dodge   F extreme"
                           : "P1: WASD Q E R SPACE SHIFT F     P2: ARROWS , . / R-SHIFT ENTER '");
  G.phase = PH_INTRO; G.phase_t = 0; G.intro_stage = 0; G.cam_snap = true;
  gui_announce(r, G.round == RT * 2 - 1 ? "FINAL ROUND" : "");
  sfx_bell(); music_set_level(1);
}

static void end_round(int A) {
  G.phase = PH_BETWEEN; G.phase_t = 0;
  if (A >= 0) G.wins[A]++; else { G.wins[0]++; G.wins[1]++; }
  gui_set_pips(0, G.wins[0]); gui_set_pips(1, G.wins[1]);
  G.match_over = G.wins[0] >= RT || G.wins[1] >= RT;
}

static void finish_match(void) {
  bool A = G.wins[0] >= RT, P = G.wins[1] >= RT;
  if (A && P) gui_set_end("DRAW", false, "NOBODY WINS. EVERYBODY BLEEDS.");
  else {
    int D = A ? 0 : 1;
    char sub[64];
    if (G.net_role) {
      if (G.has_online_names) snprintf(sub, sizeof sub, "%s TAKES THE SHOP", G.online_names[D]);
      else snprintf(sub, sizeof sub, "ONLINE VERSUS");
    } else if (G.mode == 1) snprintf(sub, sizeof sub, D == 0 ? "THE SHOP IS YOURS" : "THE SHOP WINS THIS ONE");
    else snprintf(sub, sizeof sub, "PLAYER %d TAKES THE SHOP", D + 1);
    gui_set_end(CHARS[char_index(G.sel.slots[D].ch)].name, true, sub);
    sfx_win();
  }
  gui_show("s-end"); G.phase = PH_MATCHEND; gui_clear_announce();
}

static void toggle_pause(void) {
  if (G.net_role) return;
  if (!(G.phase == PH_FIGHT || G.phase == PH_INTRO || G.phase == PH_KO || G.phase == PH_BETWEEN)) return;
  G.paused = !G.paused;
  if (G.paused) { ui_show("s-pause"); music_set_level(0.15f); }
  else { ui_hide_all(); music_set_level(1); }
}

/* ---------------------------------------------------------------- dance */

static void dance_net_send(mp_val *m);
static void dance_moved(int A, int P, int e) {
  if (G.net_role == NET_GUEST) return;
  fighter *t = G.f[A];
  if (!t) return;
  static const struct { const char *clip; float speed; } g3[4] = {{"Dodge_Left", 2}, {"Sword_Block", 2.6f}, {"Sword_UpperCut", 1.7f}, {"Dodge_Right", 2}};
  if (e == 3) fighter_play(t, "Surprise", 0.05f, true, 1.6f, 0, true);
  else fighter_play(t, g3[P].clip, 0.05f, true, g3[P].speed, 0, true);
  G.dance_idle_t[A] = ck_now() * 1000;
}

static void dance_begin(uint32_t A) {
  const char *P0 = CHARS[char_index(G.sel.slots[0].ch)].name, *P1 = CHARS[char_index(G.sel.slots[1].ch)].name;
  dance_player e[2]; int n;
  if (G.net_role == NET_HOST) { e[0] = (dance_player){P0, DP_HUMAN, 0, 0}; e[1] = (dance_player){P1, DP_REMOTE, 0, 0}; n = 2; }
  else if (G.net_role == NET_GUEST) { e[0] = (dance_player){P0, DP_REMOTE, 0, 0}; e[1] = (dance_player){P1, DP_HUMAN, 0, 0}; n = 2; }
  else if (G.mode == 2) { e[0] = (dance_player){P0, DP_HUMAN, 0, 0}; e[1] = (dance_player){P1, DP_HUMAN, 1, 0}; n = 2; }
  else { e[0] = (dance_player){P0, DP_HUMAN, 0, 0}; n = 1; }
  dance_on_move = dance_moved;
  ui_hud(false);
  dance_start(A, e, n, online_in_match() ? dance_net_send : NULL);
  music_set_level(1);
}

static double announce_clear_at = -1;
static void start_dance(void) {
  G.phase = PH_DANCE; G.phase_t = 0;
  int idx; song_pick(G.last_track, &G.dance_seed, &idx); G.last_track = idx;
  G.has_dance_res = false;
  gui_clear_announce();
  fighter *A = G.f[0], *P = G.f[1];
  fighter_reset(A); fighter_reset(P);
  t3_object_set_position(A->root, -1.3f, 0, 0); t3_object_set_position(P->root, 1.3f, 0, 0);
  A->yaw = 0; P->yaw = 0; t3_object_set_rotation(A->root, 0, 0, 0); t3_object_set_rotation(P->root, 0, 0, 0);
  trail_clear(A->trail); trail_clear(P->trail);
  G.cam_snap = true; G.dance_idle_t[0] = G.dance_idle_t[1] = 0;
  bool e = !G.net_role && G.mode == 1;
  gui_announce(e ? "DANCE BREAK" : "DANCE BATTLE", e ? "GRADE TO CHARGE EXTREME. ESC SKIPS" : "HIT THE ARROWS");
  announce_clear_at = ck_now() + 1.8;
  dance_begin(G.dance_seed);
}

static void skip_dance(void) {
  if (G.phase != PH_DANCE || G.net_role || G.mode != 1 || !dance_active() || G.has_dance_res) return;
  dance_cancel();
  memset(&G.dance_res, 0, sizeof G.dance_res);
  G.dance_res.skip = true; G.dance_res.winner = -1; G.has_dance_res = true;
}

/* ---------------------------------------------------------------- the frame */

static v2 mv_from(float mx, float my) {
  v2 P = G.cam_fwd;
  v2 t = {-P.y * mx + P.x * my, P.x * mx + P.y * my};
  float l = sqrtf(t.x * t.x + t.y * t.y);
  if (l > 1) { t.x /= l; t.y /= l; }
  return t;
}

static intent_t remote_intent(void) {
  intent_t P = {0};
  if (!G.has_remote) return P;
  P.mx = G.rem_mx; P.my = G.rem_my; P.block = G.rem_b;
  bool *dst[5] = {&P.light, &P.heavy, &P.kick, &P.dodge, &P.special};
  for (int t = 0; t < 5; t++) if (G.rem_c[t] > G.rem_seen[t]) { *dst[t] = true; G.rem_seen[t] = G.rem_c[t]; }
  return P;
}

static void play_fx(const char *k, const float *p, const float *o, const float *d, bool heavy, float dmg, bool foot, bool replay);
static void fx_ev(const char *k, const float *p, const float *o, const float *d, bool heavy, float dmg, bool foot) {
  play_fx(k, p, o, d, heavy, dmg, foot, false);
  if (HOSTING) {
    mp_val *e = mp_map(); mp_set(e, "k", mp_str(k));
    if (p) { mp_val *a = mp_arr(); for (int i = 0; i < 3; i++) mp_push(a, mp_num(p[i])); mp_set(e, "p", a); }
    if (o) { mp_val *a = mp_arr(); for (int i = 0; i < 3; i++) mp_push(a, mp_num(o[i])); mp_set(e, "o", a); }
    if (d) { mp_val *a = mp_arr(); mp_push(a, mp_num(d[0])); mp_push(a, mp_num(d[1])); mp_set(e, "d", a); }
    if (!strcmp(k, "hit")) { mp_set(e, "heavy", mp_bool(heavy)); mp_set(e, "dmg", mp_num(dmg)); mp_set(e, "pt", mp_num(foot ? 1 : 0)); }
    mp_val *w = mp_map(); mp_set(w, "k", mp_str("fx")); mp_set(w, "e", e);
    push_ev(w);
  }
}

static void project(const float p[3], float *x, float *y) { fx_project(p, x, y); }

static void play_fx(const char *k, const float *pp, const float *oo, const float *dd, bool heavy, float dmg, bool foot, bool P) {
  gv3 e = pp ? (gv3){pp[0], pp[1], pp[2]} : (gv3){0, 0, 0};
  gv3 t = oo ? (gv3){oo[0], oo[1], oo[2]} : (gv3){0, 0, 0};
  gv3 D = dd ? (gv3){dd[0], 0, dd[1]} : (gv3){0, 0, 0};
  burst_opts b = burst_defaults();
  if (!strcmp(k, "miss")) {
    rings_spawn(t, 3.4f, 15903518, 0.55f);
    G.shake = fmaxf(G.shake, 0.25f);
    if (!P) sfx_shock();
    b.speed = 4; b.life = 0.7f; b.size = 0.12f; b.color[0] = 0.8f; b.color[1] = 0.75f; b.color[2] = 0.65f; b.gravity = 2; b.drag = 2;
    parts_burst((gv3){t.x, 0.1f, t.z}, 40, &b);
  } else if (!strcmp(k, "aoe")) {
    rings_spawn(t, 3.4f, 15903518, 0.55f); rings_spawn(t, 2.2f, 15903518, 0.4f);
    if (!P) sfx_shock();
    b.speed = 5; b.life = 0.8f; b.size = 0.14f; b.color[0] = 0.85f; b.color[1] = 0.8f; b.color[2] = 0.7f; b.gravity = 2; b.drag = 2;
    parts_burst((gv3){t.x, 0.1f, t.z}, 60, &b);
  } else if (!strcmp(k, "dodge")) {
    float x, y; project(pp, &x, &y);
    ui_fx("DODGED", x, y, "dg");
    if (!P) sfx_dodge();
  } else if (!strcmp(k, "block")) {
    float x, y; project(pp, &x, &y);
    if (!P) { sfx_block(); G.freeze = fmaxf(G.freeze, 0.05f); }
    G.shake = fmaxf(G.shake, 0.08f);
    b.speed = 5; b.has_dir = true; b.dir = (gv3){-D.x, -D.y, -D.z}; b.spread = 1.2f; b.life = 0.4f; b.size = 0.05f;
    b.color[0] = 0.55f; b.color[1] = 0.75f; b.color[2] = 1; b.gravity = 6;
    parts_burst(e, 18, &b);
    bursts_spawn(e, 0.7f, true);
    ui_fx("BLOCKED", x, y, "blk");
  } else if (!strcmp(k, "hit")) {
    float x, y; project(pp, &x, &y);
    bool g = heavy;
    if (!P) { sfx_hit(g); if (foot) sfx_kick(); G.freeze = fmaxf(G.freeze, fminf(0.15f, 0.045f + dmg * 6e-3f)); }
    G.shake = fmaxf(G.shake, fminf(0.45f, 0.1f + dmg * 0.02f));
    b.speed = g ? 8 : 6; b.has_dir = true; b.dir = D; b.spread = 1; b.life = 0.55f; b.size = 0.06f;
    b.color[0] = 1; b.color[1] = 0.72f; b.color[2] = 0.25f; b.gravity = 9; b.drag = 0.7f;
    parts_burst(e, g ? 44 : 26, &b);
    burst_opts c = burst_defaults();
    c.speed = 2.5f; c.life = 0.7f; c.size = 0.18f; c.color[0] = 0.7f; c.color[1] = 0.65f; c.color[2] = 0.58f; c.gravity = -0.5f; c.drag = 2;
    parts_burst(e, 10, &c);
    bursts_spawn(e, g ? 1.3f : 0.85f, false);
    char s[16]; snprintf(s, sizeof s, "%d", (int)lroundf(dmg));
    ui_fx(s, x, y, g ? "big" : "");
  } else if (!strcmp(k, "ko")) {
    b.speed = 10; b.life = 0.9f; b.size = 0.08f; b.color[0] = 1; b.color[1] = 0.6f; b.color[2] = 0.2f; b.gravity = 8;
    parts_burst(e, 70, &b);
    G.shake = 0.6f;
  }
}

static void resolve(fighter *A, fighter *P) {
  strike_t e = fighter_strike(A, P);
  if (!e.ok) return;
  if (A->move < 0) return;
  const move_def *t = &MOVES[A->move];
  float D[3] = {e.origin.x, e.origin.y, e.origin.z};
  if (e.miss) { fx_ev("miss", NULL, D, NULL, false, 0, false); return; }
  if (e.aoe) { fx_ev("aoe", NULL, D, NULL, false, 0, false); gui_flash(0xf2ab1e, 0.45f, 260); }
  float w = t->dmg * A->W->dmg, g = A->W->dmg > 1.3f ? 1.25f : 1;
  hit_in h = {w, t->kb * g, e.dir, t->stun, t->react, t->breaks};
  hit_out i = fighter_take_hit(P, &h);
  float r[3] = {e.point.x, e.point.y, e.point.z}, v[2] = {e.dir.x, e.dir.z};
  if (i.r == HIT_DODGED) { fx_ev("dodge", r, NULL, NULL, false, 0, false); return; }
  if (i.r == HIT_BLOCKED) {
    A->vel.x += e.dir.x * -1.5f; A->vel.y += e.dir.y * -1.5f; A->vel.z += e.dir.z * -1.5f;
    fx_ev("block", r, NULL, v, false, 0, false);
    return;
  }
  fighter_on_land(A, A->move);
  bool o = t->heavy || w > 9;
  fx_ev("hit", r, NULL, v, o, i.dmg, t->foot);
  if (o) gui_flash(0xffffff, 0.12f, 120);
  if (A->combo >= 2) gui_combo(A->slot, A->combo, A->combo >= 5 ? "ABSOLUTELY EXTREME" : A->combo >= 3 ? "NICE" : "");
  gui_combo(P->slot, 0, NULL);
  if (i.r == HIT_KO) {
    G.phase = PH_KO; G.phase_t = 0; G.slow = 0.28f; G.freeze = 0.25f; G.shake = 0.6f;
    G.ko_winner = A->slot; G.perfect = A->hp >= 99.9f;
    gui_announce("K.O.", G.perfect ? "PERFECT" : "");
    sfx_ko(); gui_flash(0xffffff, 0.6f, 300);
    fx_ev("ko", r, NULL, NULL, false, 0, false);
  }
}

static void separate(void) {
  fighter *A = G.f[0], *P = G.f[1];
  v3 *a = fighter_pos(A), *p = fighter_pos(P);
  float e = p->x - a->x, t = p->z - a->z, D = hypotf(e, t); if (D == 0) D = 1e-3f;
  const float w = 0.95f;
  if (D < w) {
    float g = (w - D) / 2, i = e / D, n = t / D;
    a->x -= i * g; a->z -= n * g; p->x += i * g; p->z += n * g;
  }
  for (int k = 0; k < 2; k++) {
    fighter *g = G.f[k];
    v3 *q = fighter_pos(g);
    float i = hypotf(q->x, q->z);
    if (i > DV) {
      float n = DV / i;
      q->x *= n; q->z *= n;
      float r = q->x / DV, v = q->z / DV, o = g->vel.x * r + g->vel.z * v;
      if (o > 0) { g->vel.x -= r * o; g->vel.z -= v * o; }
    }
  }
}

static void phase_logic(float A) {
  fighter *P = G.f[0], *e = G.f[1];
  switch (G.phase) {
    case PH_INTRO:
      if (G.intro_stage == 0 && G.phase_t > 1.4f) { G.intro_stage = 1; gui_announce("FIGHT!", ""); sfx_fight(); }
      if (G.phase_t > 2) { G.phase = PH_FIGHT; G.phase_t = 0; }
      break;
    case PH_FIGHT:
      G.time_left -= A; ui_set_timer(G.time_left);
      if (G.time_left <= 0) {
        G.time_left = 0; ui_set_timer(0);
        int t = -1;
        if (P->hp > e->hp + 0.01f) t = 0; else if (e->hp > P->hp + 0.01f) t = 1;
        gui_announce("TIME", "UP"); sfx_bell();
        if (t >= 0) { fighter_victory(G.f[t]); fighter_intro(G.f[1 - t]); }
        G.ko_winner = t; end_round(t);
      }
      break;
    case PH_KO:
      G.slow = ck_lerp(0.28f, 1, fminf(1, fmaxf(0, (G.phase_t - 0.2f) / 1.6f)));
      if (G.phase_t > 1.5f && G.ko_winner >= 0 && G.f[G.ko_winner]->state != ST_WIN) fighter_victory(G.f[G.ko_winner]);
      if (G.phase_t > 3) { G.slow = 1; end_round(G.ko_winner); }
      break;
    case PH_BETWEEN:
      if (G.phase_t > 2.6f) {
        gui_clear_announce();
        if (G.match_over) finish_match();
        else if (G.dance_on && G.net_role != NET_GUEST) start_dance();
        else { G.round++; start_round(); }
      }
      break;
    default: break;
  }
}

static void update_trails(void) {
  for (int k = 0; k < 2; k++) {
    fighter *P = G.f[k];
    int e = P->move;
    if (P->state == ST_ATTACK && e >= 0 && !P->recovering && P->cur && P->cur->time > MOVES[e].hit0 - 0.08f &&
        P->cur->time < MOVES[e].hit1 + 0.1f && !MOVES[e].foot && P->weapon_obj) {
      v3 D, w; fighter_weapon_segment(P, &D, &w);
      trail_push(P->trail, (gv3){D.x, D.y, D.z}, (gv3){w.x, w.y, w.z});
    } else trail_decay(P->trail);
    trail_update(P->trail);
  }
}

static void update_hud(void) {
  for (int e = 0; e < 2; e++) {
    fighter *P = G.f[e];
    ui_set_health(e, P->hp, false); ui_set_meter(e, P->meter);
    if (P->combo < 2) ui_combo(e, 0, NULL);
  }
}

static void update_camera(float A) {
  fighter *P = G.f[0], *e = G.f[1];
  v3 *pp = fighter_pos(P), *ep = fighter_pos(e);
  v3 w, g;
  v3 n = {(pp->x + ep->x) / 2, 0, (pp->z + ep->z) / 2};
  float r = hypotf(pp->x - ep->x, pp->z - ep->z);
  if (G.phase == PH_MENU || G.phase == PH_LOAD) {
    G.menu_orbit += A * 0.12f;
    const char *scr = ui_screen();
    bool v = scr && !strcmp(scr, "s-select");
    float o = v ? 6.6f : 7.4f, B = v ? 1.7f : 1.9f, z = G.menu_orbit * (v ? 0.35f : 1);
    w = (v3){sinf(z) * o * 0.9f, B, cosf(z) * o};
    float a = v ? 0 : -1.9f;
    v3 c = {cosf(z), 0, -sinf(z)};
    g = (v3){-c.x * a, (v ? 0.55f : 1.25f) - c.y * a, -c.z * a};
    if (v) { w = (v3){sinf(z) * 5.6f, 1.5f, cosf(z) * 5.6f + 0.6f}; g = (v3){0, 0.35f, 0}; }
  } else if (G.phase == PH_DANCE) {
    w = (v3){0, 1.5f, 5.7f}; g = (v3){0, 1.1f, 0};
  } else {
    float vx = ep->x - pp->x, vy = ep->z - pp->z;
    float vl = sqrtf(vx * vx + vy * vy);
    if (vl < 0.2f) { vx = 1; vy = 0; vl = 1; }
    vx /= vl; vy /= vl;
    float o = atan2f(vy, vx);
    if (G.cam_snap) G.cam_yaw = o;
    float B = ck_wrap_pi(o - G.cam_yaw);
    G.cam_yaw += B * fminf(1, A * 3.2f);
    float zx = cosf(G.cam_yaw), zy = sinf(G.cam_yaw);
    float a = ck_clamp(3.9f + r * 0.78f, 4.8f, 8), c = 1.55f + r * 0.1f, Q = 1.05f;
    if (G.phase == PH_KO) {
      int kw = G.ko_winner >= 0 ? G.ko_winner : 0;
      fighter *u = G.f[1 - kw];
      v3 *up = fighter_pos(u);
      float T = fminf(1, G.phase_t * 0.8f);
      a = ck_lerp(a, 3.4f, T); c = ck_lerp(c, 1.1f, T);
      n.x += (up->x - n.x) * 0.4f * T; n.z += (up->z - n.z) * 0.4f * T;
    }
    for (int k = 0; k < 2; k++) if (G.f[k]->state == ST_ATTACK && G.f[k]->move == MV_SPECIAL) { a *= 0.85f; break; }
    w = (v3){n.x - zy * a, c, n.z + zx * a}; g = (v3){n.x, Q, n.z};
    w.x = ck_clamp(w.x, -ROOM_HX + 1, ROOM_HX - 1); w.z = ck_clamp(w.z, -ROOM_HZ + 1, ROOM_HZ - 1);
  }
  if (G.cam_snap || G.phase == PH_LOAD) { G.cs.pos = w; G.cs.look = g; G.cam_snap = false; }
  else {
    float v = 1 - expf(-A * (G.phase == PH_MENU ? 3 : 6));
    G.cs.pos.x += (w.x - G.cs.pos.x) * v; G.cs.pos.y += (w.y - G.cs.pos.y) * v; G.cs.pos.z += (w.z - G.cs.pos.z) * v;
    G.cs.look.x += (g.x - G.cs.look.x) * v; G.cs.look.y += (g.y - G.cs.look.y) * v; G.cs.look.z += (g.z - G.cs.look.z) * v;
  }
  v3 p = G.cs.pos;
  if (G.shake > 0) {
    p.x += (ck_rand() - 0.5f) * G.shake * 0.35f; p.y += (ck_rand() - 0.5f) * G.shake * 0.3f; p.z += (ck_rand() - 0.5f) * G.shake * 0.35f;
  }
  t3_object_set_position(G.cam, p.x, p.y, p.z);
  t3_object_look_at(G.cam, G.cs.look.x, G.cs.look.y, G.cs.look.z);
}

static void host_tick(float A);
static void post_fx(float A, float P) {
  fighter *e = G.f[0], *t = G.f[1];
  update_trails();
  parts_update(A * fmaxf(0.4f, G.slow));
  bursts_update(A); rings_update(A);
  if (G.arena_ready) {
    v3 *a = fighter_pos(e), *b = fighter_pos(t);
    arena_set_focus((a->x + b->x) / 2, 1.2f, (a->z + b->z) / 2);
    arena_update(A, G.t);
  }
  update_hud();
  update_camera(A);
  G.shake *= powf(1e-3f, A);
  if (G.shake < 2e-3f) G.shake = 0;
  (void)P;
}

static void step_guest(float A);
static void step_dance(float A);

static void step(float A) {
  /* camFwd: the camera's world direction, flat */
  t3_object *c = (t3_object *)G.cam;
  float fx = -c->matrix_world.e[8], fz = -c->matrix_world.e[10], fl = sqrtf(fx * fx + fz * fz);
  G.cam_fwd = fl > 0 ? (v2){fx / fl, fz / fl} : (v2){0, -1};
  if (G.net_role == NET_GUEST) { step_guest(A); return; }
  if (G.phase == PH_DANCE) { step_dance(A); return; }
  float P;
  if (G.freeze > 0) { G.freeze -= A; P = 0; } else P = A * G.slow;
  fighter *e = G.f[0], *t = G.f[1];
  bool D = G.phase == PH_FIGHT;
  intent_t i[2] = {{0}, {0}}; v2 n[2] = {{0, 0}, {0, 0}};
  if (D) {
    i[0] = input_poll(0); n[0] = mv_from(i[0].mx, i[0].my);
    if (G.net_role == NET_HOST) { i[1] = remote_intent(); n[1] = mv_from(i[1].mx, i[1].my); }
    else if (G.mode == 2) { i[1] = input_poll(1); n[1] = mv_from(i[1].mx, i[1].my); }
    else if (G.has_brain) {
      v2 wv; i[1] = brain_think(&G.br, P, t, e, &wv);
      float l = sqrtf(wv.x * wv.x + wv.y * wv.y); if (l > 1) { wv.x /= l; wv.y /= l; }
      n[1] = wv;
    }
  }
  float r = hypotf(fighter_pos(e)->x - fighter_pos(t)->x, fighter_pos(e)->z - fighter_pos(t)->z);
  fight_ctx ctx = {D, r};
  fighter_update(e, P, &i[0], n[0], t, &ctx);
  fighter_update(t, P, &i[1], n[1], e, &ctx);
  separate();
  if (D) { resolve(e, t); resolve(t, e); }
  G.phase_t += A;
  phase_logic(A);
  post_fx(A, P);
  if (G.net_role == NET_HOST) host_tick(A);
}

static void step_dance(float A) {
  G.phase_t += A;
  if (dance_active()) dance_update(A);
  double t = ck_now() * 1000;
  for (int w = 0; w < 2; w++) {
    fighter *D = G.f[w];
    if (D->cur && t - G.dance_idle_t[w] > 650 && strcmp(fighter_clip_name(D), "Sword_Idle"))
      fighter_play(D, "Sword_Idle", 0.2f, false, 1, 0, true);
    mixer_update(&D->mixer, A);
    t3_object_update_matrix_world(D->root, true);
  }
  dance_result r;
  if (!G.has_dance_res && dance_take_result(&r)) { G.dance_res = r; G.has_dance_res = true; }
  if (G.has_dance_res) {
    dance_result D = G.dance_res;
    G.has_dance_res = false;
    ui_hud(true); G.round++; start_round();
    char s[64];
    if (D.skip) gui_toast("DANCE SKIPPED");
    else if (D.solo) {
      fighter *w = G.f[0];
      w->meter = (float)D.meter; ui_set_meter(0, (float)D.meter);
      if (D.meter) snprintf(s, sizeof s, "DANCE GRADE %s  EXTREME +%d", D.grade, D.meter);
      else snprintf(s, sizeof s, "DANCE GRADE %s", D.grade);
      gui_toast(s);
    } else if (D.winner >= 0) {
      fighter *w = G.f[D.winner];
      w->meter = 70; ui_set_meter(D.winner, 70);
      snprintf(s, sizeof s, "%s STARTS CHARGED", CHARS[char_index(G.sel.slots[D.winner].ch)].name);
      gui_toast(s);
    }
  }
  post_fx(A, A);
  if (G.net_role == NET_HOST) host_tick(A);
}

/* ---------------------------------------------------------------- online: host snapshots, guest replay */

static double r3(double v) { return round(v * 1000) / 1000; }
static double r2(double v) { return round(v * 100) / 100; }

static mp_val *snapshot(void) {
  mp_val *f = mp_arr();
  for (int i = 0; i < 2; i++) {
    fighter *e = G.f[i];
    ck_action *t = e->cur;
    mp_val *a = mp_arr();
    v3 *p = fighter_pos(e);
    mp_push(a, mp_num(r3(p->x))); mp_push(a, mp_num(r3(p->z))); mp_push(a, mp_num(r3(e->yaw)));
    mp_push(a, mp_num(r2(e->hp))); mp_push(a, mp_num(r2(e->meter)));
    mp_push(a, mp_str(STATE_NAMES[e->state]));
    mp_push(a, mp_str(e->move >= 0 ? MOVES[e->move].id : ""));
    mp_push(a, mp_str(t ? fighter_clip_name(e) : ""));
    mp_push(a, mp_num(t ? r3(t->time) : 0));
    mp_push(a, mp_num(t ? r2(t->time_scale) : 1));
    mp_push(a, mp_num(t && t->loop == LOOP_ONCE ? 1 : 0));
    mp_push(a, mp_num(e->play_count));
    mp_push(a, mp_num(e->recovering ? 1 : 0));
    mp_push(a, mp_num(e->combo));
    mp_push(a, mp_str(e->weapon_key >= 0 ? WEAPONS[e->weapon_key].key : ""));
    mp_push(f, a);
  }
  double P = ck_now() * 1000;
  int k = 0;
  for (int i = 0; i < G.nev; i++) { if (P - G.evbuf[i].t < 800) G.evbuf[k++] = G.evbuf[i]; else mp_free(G.evbuf[i].e); }
  G.nev = k;
  mp_val *s = mp_map();
  mp_set(s, "n", mp_num(++G.snap_n)); mp_set(s, "ph", mp_str(PH_NAMES[G.phase])); mp_set(s, "pt", mp_num(r3(G.phase_t)));
  mp_set(s, "rd", mp_num(G.round));
  mp_val *w = mp_arr(); mp_push(w, mp_num(G.wins[0])); mp_push(w, mp_num(G.wins[1])); mp_set(s, "w", w);
  mp_set(s, "tl", mp_num(r2(G.time_left)));
  mp_set(s, "kw", G.ko_winner >= 0 ? mp_num(G.ko_winner) : mp_nil());
  mp_set(s, "sl", mp_num(r2(G.slow)));
  mp_set(s, "ds", mp_num(G.phase == PH_DANCE ? G.dance_seed : 0));
  mp_set(s, "f", f);
  mp_val *ev = mp_arr();
  for (int i = 0; i < G.nev; i++) mp_push(ev, mp_clone(G.evbuf[i].e));
  mp_set(s, "ev", ev);
  return s;
}

static void host_tick(float A) {
  G.snap_timer += A;
  if (G.snap_timer >= 0.04) { G.snap_timer = 0; online_snap(snapshot()); }
  if (online_idle_ms() > (G.idle_ms ? G.idle_ms : 1e4)) game_leave_online("OPPONENT LEFT");
}

void game_on_intent(const mp_val *A) {
  if (!G.has_remote) {
    G.has_remote = true;
    const mp_val *c = mp_get(A, "c");
    static const char *K[5] = {"l", "h", "k", "d", "s"};
    for (int i = 0; i < 5; i++) G.rem_c[i] = G.rem_seen[i] = (int)mp_getn(c, K[i], 0);
  }
  G.rem_mx = (float)mp_getn(A, "mx", 0); G.rem_my = (float)mp_getn(A, "my", 0);
  G.rem_b = mp_getn(A, "b", 0) != 0;
  const mp_val *c = mp_get(A, "c");
  if (c) { static const char *K[5] = {"l", "h", "k", "d", "s"}; for (int i = 0; i < 5; i++) G.rem_c[i] = (int)mp_getn(c, K[i], G.rem_c[i]); }
}

static void apply_ui_ev(const char *m, const mp_val *a) {
  const mp_val *a0 = mp_at(a, 0), *a1 = mp_at(a, 1), *a2 = mp_at(a, 2);
  const char *s0 = a0 && a0->t == MP_STR ? a0->s : "", *s1 = a1 && a1->t == MP_STR ? a1->s : "";
  double n0 = a0 && a0->t == MP_NUM ? a0->n : 0, n1 = a1 && a1->t == MP_NUM ? a1->n : 0, n2 = a2 && a2->t == MP_NUM ? a2->n : 0;
  if (!strcmp(m, "show")) ui_show(s0);
  else if (!strcmp(m, "announce")) ui_announce(s0, s1, false);
  else if (!strcmp(m, "clearAnnounce")) ui_clear_announce();
  else if (!strcmp(m, "setPips")) ui_set_pips((int)n0, (int)n1, a2 ? (int)n2 : 2);
  else if (!strcmp(m, "setRound")) ui_set_round(s0);
  else if (!strcmp(m, "setNames")) {
    const mp_val *l = a2; const mp_val *l0 = mp_at(l, 0), *l1 = mp_at(l, 1);
    ui_set_names(s0, s1, l0 && l0->t == MP_STR ? l0->s : NULL, l1 && l1->t == MP_STR ? l1->s : NULL);
  } else if (!strcmp(m, "combo")) ui_combo((int)n0, (int)n1, a2 && a2->t == MP_STR ? a2->s : NULL);
  else if (!strcmp(m, "flash")) {
    uint32_t c = 0xf2ab1e; if (s0[0] == '#') c = (uint32_t)strtoul(s0 + 1, NULL, 16);
    ui_flash(c, a1 ? (float)n1 : 0.5f, a2 ? (float)n2 : 180);
  } else if (!strcmp(m, "setEnd")) {
    char name[48]; snprintf(name, sizeof name, "%s", s0);
    char *em = strstr(name, " <em>");
    if (em) { *em = 0; ui_set_end(name, true, s1); } else ui_set_end(name, false, s1);
  } else if (!strcmp(m, "hideAll")) ui_hide_all();
  else if (!strcmp(m, "hud")) ui_hud(mp_truthy(a0));
  else if (!strcmp(m, "setHint")) ui_set_hint(s0);
  else if (!strcmp(m, "toast")) ui_toast(s0);
}

static void fx_from(const mp_val *e) {
  const char *k = mp_gets(e, "k", "");
  float p[3], o[3], d[2]; bool hp = false, ho = false, hd = false;
  const mp_val *v;
  if ((v = mp_get(e, "p")) && v->t == MP_ARR) { for (int i = 0; i < 3; i++) p[i] = mp_at(v, i) ? (float)mp_at(v, i)->n : 0; hp = true; }
  if ((v = mp_get(e, "o")) && v->t == MP_ARR) { for (int i = 0; i < 3; i++) o[i] = mp_at(v, i) ? (float)mp_at(v, i)->n : 0; ho = true; }
  if ((v = mp_get(e, "d")) && v->t == MP_ARR) { for (int i = 0; i < 2; i++) d[i] = mp_at(v, i) ? (float)mp_at(v, i)->n : 0; hd = true; }
  play_fx(k, hp ? p : NULL, ho ? o : NULL, hd ? d : NULL, mp_truthy(mp_get(e, "heavy")), (float)mp_getn(e, "dmg", 0), mp_getn(e, "pt", 0) != 0, true);
}

void game_on_snap(const mp_val *A) {
  if (G.snap) mp_free(G.snap);
  G.snap = mp_clone(A); G.snap_at = ck_now() * 1000;
  const mp_val *ev = mp_get(A, "ev");
  for (int i = 0; ev && i < ev->count; i++) {
    const mp_val *P = ev->items[i];
    int id = (int)mp_getn(P, "i", 0);
    if (id <= G.last_ev) continue;
    G.last_ev = id;
    const char *k = mp_gets(P, "k", "");
    if (!strcmp(k, "sfx")) { const mp_val *a = mp_get(P, "a"); const mp_val *a0 = mp_at(a, 0); sfx_by_name(mp_gets(P, "m", ""), a0 && a0->t == MP_NUM ? (float)a0->n : 1); }
    else if (!strcmp(k, "ui")) apply_ui_ev(mp_gets(P, "m", ""), mp_get(P, "a"));
    else if (!strcmp(k, "fx")) fx_from(mp_get(P, "e"));
  }
}

static void start_dance_guest(uint32_t A) {
  if (dance_active() || G.dance_seed_done == A) return;
  G.dance_seed_done = A;
  dance_begin(A);
}

static void step_guest(float A) {
  const mp_val *P = G.snap;
  intent_t e = input_poll(0);
  int *t = G.guest_counts;
  if (e.light) t[0]++; if (e.heavy) t[1]++; if (e.kick) t[2]++; if (e.dodge) t[3]++; if (e.special) t[4]++;
  G.int_timer += A;
  if (G.int_timer >= 0.033 && online_in_match()) {
    G.int_timer = 0;
    mp_val *m = mp_map();
    mp_set(m, "mx", mp_num(round(e.mx * 100) / 100)); mp_set(m, "my", mp_num(round(e.my * 100) / 100));
    mp_set(m, "b", mp_num(e.block ? 1 : 0));
    mp_val *c = mp_map(); static const char *K[5] = {"l", "h", "k", "d", "s"};
    for (int i = 0; i < 5; i++) mp_set(c, K[i], mp_num(t[i]));
    mp_set(m, "c", c);
    online_intent(m);
  }
  if (online_in_match() && online_idle_ms() > (G.idle_ms ? G.idle_ms : 1e4)) { game_leave_online("HOST LEFT"); return; }
  if (!P) return;
  int ph = ph_index(mp_gets(P, "ph", "menu"));
  if (ph != G.phase) {
    G.phase = ph; G.phase_t = (float)mp_getn(P, "pt", 0);
    if (ph == PH_INTRO) { G.cam_snap = true; if (dance_active()) dance_cancel(); }
    if (ph == PH_DANCE) start_dance_guest((uint32_t)mp_getn(P, "ds", 0));
  } else G.phase_t += A;
  const mp_val *kw = mp_get(P, "kw");
  G.ko_winner = kw && kw->t == MP_NUM ? (int)kw->n : -1;
  G.slow = (float)mp_getn(P, "sl", 1); G.round = (int)mp_getn(P, "rd", 1);
  const mp_val *w = mp_get(P, "w");
  if (w) { G.wins[0] = (int)mp_at(w, 0)->n; G.wins[1] = (int)mp_at(w, 1)->n; }
  G.time_left = (float)mp_getn(P, "tl", 0);
  if (G.phase == PH_FIGHT) ui_set_timer(G.time_left);
  float D = 1 - expf(-A * 22), wt = (float)((ck_now() * 1000 - G.snap_at) / 1000);
  const mp_val *f = mp_get(P, "f");
  for (int i = 0; i < 2; i++) {
    fighter *g = G.f[i];
    const mp_val *n = mp_at(f, i);
    if (!n) continue;
    #define N(k) (mp_at(n, k) && mp_at(n, k)->t == MP_NUM ? (float)mp_at(n, k)->n : 0)
    #define S(k) (mp_at(n, k) && mp_at(n, k)->t == MP_STR ? mp_at(n, k)->s : "")
    v3 *q = fighter_pos(g);
    if (hypotf(N(0) - q->x, N(1) - q->z) > 2.5f) { q->x = N(0); q->z = N(1); }
    q->x += (N(0) - q->x) * D; q->z += (N(1) - q->z) * D;
    float r = ck_wrap_pi(N(2) - g->yaw);
    g->yaw += r * D; t3_object_set_rotation(g->root, 0, g->yaw, 0);
    g->hp = N(3); g->meter = N(4); g->state = state_index(S(5));
    g->move = move_index(S(6)); g->recovering = N(12) != 0; g->combo = (int)N(13);
    int wk = weapon_index(S(14));
    if (wk >= 0 && g->weapon_key != wk) { fighter_set_weapon(g, wk); trail_set_color(g->trail, g->W->trail); }
    const char *clip = S(7);
    if (clip[0]) {
      int cidx = rig_clip_index(g->rig, clip);
      if (g->g_play != (int)N(11) || g->g_clip != cidx) {
        g->g_play = (int)N(11); g->g_clip = cidx;
        fighter_play(g, clip, 0.08f, N(10) != 0, N(9), N(8), true);
      }
      if (g->cur) {
        g->cur->time_scale = N(9);
        if (N(10) != 0) {
          float v = N(8) + wt * N(9);
          if (fabsf(g->cur->time - v) > 0.2f) g->cur->time = v;
        }
      }
    }
    #undef N
    #undef S
    mixer_update(&g->mixer, A);
    t3_object_update_matrix_world(g->root, true);
  }
  post_fx(A, A);
  if (dance_active()) dance_update(A);
}

void game_on_ctl(const mp_val *A) {
  const char *k = mp_gets(A, "k", "");
  if (!strcmp(k, "bye")) game_leave_online("OPPONENT LEFT");
  else if (!strcmp(k, "rm")) { ui_toast("REMATCH REQUESTED"); G.rematch_asked = true; }
  else if (!strcmp(k, "dh") && dance_active()) dance_remote_hit((int)mp_getn(A, "i", 0), (int)mp_getn(A, "j", 0), (int)mp_getn(A, "sc", 0));
  else if (!strcmp(k, "df") && dance_active()) dance_remote_final((int)mp_getn(A, "sc", 0), (int)mp_getn(A, "mc", 0));
}

static void dance_net_send(mp_val *m) { online_ctl(m); }

void game_begin_online(int role, const char *my_name, const char *my_weapon, const char *opp_name, const char *opp_weapon) {
  G.sel_backup = G.sel; G.has_backup = true;
  G.net_role = role; G.mode = 1; input_mode = 1;
  G.has_remote = false;
  if (G.snap) { mp_free(G.snap); G.snap = NULL; }
  G.last_ev = 0; G.snap_n = 0;
  for (int i = 0; i < G.nev; i++) mp_free(G.evbuf[i].e);
  G.nev = 0;
  bool t = role == NET_HOST;
  snprintf(G.sel.slots[0].ch, 8, "moheeb"); snprintf(G.sel.slots[0].weapon, 12, "%s", t ? my_weapon : opp_weapon);
  snprintf(G.sel.slots[1].ch, 8, "luis"); snprintf(G.sel.slots[1].weapon, 12, "%s", t ? opp_weapon : my_weapon);
  G.has_online_names = true;
  snprintf(G.online_names[0], 16, "%s", t ? my_name : opp_name);
  snprintf(G.online_names[1], 16, "%s", t ? opp_name : my_name);
  G.has_brain = false; G.paused = false;
  memset(G.guest_counts, 0, sizeof G.guest_counts); G.int_timer = 0;
  apply_sel();
  ui_hide_all(); ui_hud(true);
  if (t) { music_set_level(1); start_match(); }
  else {
    G.phase = PH_INTRO; G.cam_snap = true; G.round = 1; G.wins[0] = G.wins[1] = 0;
    music_set_level(1); ui_set_pips(0, 0, RT); ui_set_pips(1, 0, RT);
  }
}

void game_leave_online(const char *A) {
  if (!G.net_role) return;
  G.net_role = NET_NONE; G.has_online_names = false; G.has_remote = false;
  if (dance_active()) dance_cancel();
  music_set_level(0.35f);
  online_end_match();
  if (G.has_backup) { G.sel = G.sel_backup; G.has_backup = false; apply_sel(); }
  menu_mode();
  /* P.onLeave: the HUD off and the online screen open, with the reason */
  ui_hud(false);
  online_leave_room_status(A);
}

/* ---------------------------------------------------------------- the app (the page's main) */

static void act(const char *v) {
  if (!strcmp(v, "1p") || !strcmp(v, "2p")) {
    G.mode = v[0] == '1' ? 1 : 2; input_mode = G.mode;
    ui_set_select(&G.sel, G.mode); apply_sel(); ui_show("s-select");
  } else if (!strcmp(v, "online")) online_open();
  else if (!strcmp(v, "onback")) { online_back(); ui_show("s-title"); }
  else if (!strcmp(v, "ollobby")) online_main_lobby();
  else if (!strcmp(v, "olnew")) online_new_code();
  else if (!strcmp(v, "oljoin")) online_join();
  else if (!strcmp(v, "olcopy")) online_copy_link();
  else if (!strcmp(v, "padcopy")) online_copy_pad();
  else if (!strcmp(v, "olaccept")) online_accept();
  else if (!strcmp(v, "oldecline")) online_decline();
  else if (!strcmp(v, "pads")) online_open_pads();
  else if (!strcmp(v, "padsback")) ui_show("s-title");
  else if (!strcmp(v, "controls")) { snprintf(G.screen_before_controls, 16, "%s", ui_screen() ? ui_screen() : "s-title"); ui_show("s-controls"); }
  else if (!strcmp(v, "controls2")) { snprintf(G.screen_before_controls, 16, "s-pause"); ui_show("s-controls"); }
  else if (!strcmp(v, "back")) {
    const char *s = ui_screen();
    if (s && !strcmp(s, "s-controls")) ui_show(G.screen_before_controls); else ui_show("s-title");
  } else if (!strcmp(v, "start")) start_match();
  else if (!strcmp(v, "resume")) toggle_pause();
  else if (!strcmp(v, "quit")) { G.paused = false; menu_mode(); ui_show("s-title"); }
  else if (!strcmp(v, "rematch")) {
    if (G.net_role == NET_GUEST) { mp_val *c = mp_map(); mp_set(c, "k", mp_str("rm")); online_ctl(c); ui_toast("REMATCH REQUESTED"); }
    else start_match();
  } else if (!strcmp(v, "again")) {
    if (G.net_role) { game_leave_online(""); return; }
    G.paused = false; menu_mode(); ui_set_select(&G.sel, G.mode); ui_show("s-select");
  }
}
static void cb_weapon(int p, const char *w) { snprintf(G.sel.slots[p].weapon, 12, "%s", w); apply_sel(); }
static void cb_swap(void) {
  char a[8]; snprintf(a, 8, "%s", G.sel.slots[0].ch);
  snprintf(G.sel.slots[0].ch, 8, "%s", G.sel.slots[1].ch); snprintf(G.sel.slots[1].ch, 8, "%s", a);
  apply_sel();
}
static void cb_diff(int l) { G.sel.level = l; }

/* the window keydown handler of the page: Escape skips the dance, Escape or P
 * pauses, M mutes, N toggles the music */
static void app_key(int c) {
  if (c == WC_KEY_ESCAPE && G.phase == PH_DANCE) { skip_dance(); return; }
  if (c == WC_KEY_ESCAPE || c == WC_KEY_P) {
    const char *s = ui_screen();
    if (s && !strcmp(s, "s-pause")) toggle_pause();
    else if (!s) toggle_pause();
  }
  if (c == WC_KEY_M) ui_toast(sfx_toggle_mute() ? "MUTED" : "SOUND ON");
  if (c == WC_KEY_N) ui_toast(music_toggle() ? "MUSIC ON" : "MUSIC OFF");
}

/* the cabinet's pads in the menus (the page leaves menus to the keyboard and
 * mouse): the stick and d-pad are the arrows, A is Enter, B is Escape, Y is
 * Tab (swap sides), Start is Enter on a menu and Escape (pause) in a fight */
static void pad_keys(void) {
  const char *s = ui_screen();
  for (int p = 0; p < 2; p++) {
    if (!input_pad_connected(p)) continue;
    bool menu = s != NULL;
    if (menu) {
      int dx, dy; input_pad_nav(p, &dx, &dy);
      if (dy < 0) input_push_key(WC_KEY_UP);
      if (dy > 0) input_push_key(WC_KEY_DOWN);
      if (dx < 0) input_push_key(WC_KEY_LEFT);
      if (dx > 0) input_push_key(WC_KEY_RIGHT);
      if (input_pad_pressed(p, WC_BTN_A)) input_push_key(WC_KEY_ENTER);
      if (input_pad_pressed(p, WC_BTN_B)) input_push_key(WC_KEY_ESCAPE);
      if (input_pad_pressed(p, WC_BTN_Y)) input_push_key(WC_KEY_TAB);
      if (input_pad_pressed(p, WC_BTN_START)) input_push_key(!strcmp(s, "s-pause") ? WC_KEY_ESCAPE : WC_KEY_ENTER);
    } else {
      if (input_pad_pressed(p, WC_BTN_START)) input_push_key(WC_KEY_ESCAPE);
      if (G.phase == PH_DANCE && input_pad_pressed(p, WC_BTN_B)) input_push_key(WC_KEY_ESCAPE);
    }
  }
}

/* ---------------------------------------------------------------- main.c's entry points */

void game_init(void) {
  memset(&G, 0, sizeof G);
  G.phase = PH_LOAD; G.mode = 1; G.dance_on = true; G.last_track = -1; G.ko_winner = -1; G.slow = 1;
  snprintf(G.sel.slots[0].ch, 8, "moheeb"); snprintf(G.sel.slots[0].weapon, 12, "pipe");
  snprintf(G.sel.slots[1].ch, 8, "luis"); snprintf(G.sel.slots[1].weapon, 12, "sledge");
  G.sel.level = 1;
  fx_init(); gfx_init(); ui_init(); dance_init(); online_init();
  ui_cb.act = act; ui_cb.weapon = cb_weapon; ui_cb.swap = cb_swap; ui_cb.diff = cb_diff;
  sfx_tap = sfx_tapped;
  G.scene = t3_scene_new();
  arena_scene_setup(CK_R, G.scene);
  /* new Ae(40, 1, .1, 80) at (0, 2, 7); resize: fov 40 at 16:9 */
  G.cam = t3_perspective_camera_new(40, (float)CK_W / CK_H, 0.1f, 80);
  t3_object_set_position(G.cam, 0, 2, 7);
  G.cs.pos = (v3){0, 2, 7}; G.cs.look = (v3){0, 1.2f, 0};
  gfx_set_scale(CK_H, 40);
  ui_show("s-load");
}

/* init(A) of the page: the load bar moves through these */
bool game_boot(int stage) {
  switch (stage) {
    case 0: ui_load(0.05f, "loading fighters"); return false;
    case 1: G.chars[1] = fighter_new("a", "LUIS", 1.04f, "char_a.glb", "rig_a.bin"); return false;
    case 2: G.chars[0] = fighter_new("b", "MOHEEB", 0.97f, "char_b.glb", "rig_b.bin"); ui_load(0.35f, "retargeting animation"); return false;
    case 3: ui_load(0.5f, "building the hackerspace"); return false;
    default: break;
  }
  if (!G.arena_ready) {
    if (!arena_build(G.scene, stage - 4)) return false;
    G.arena_ready = true;
    ui_load(0.8f, "lighting up");
    return false;
  }
  for (int i = 0; i < 2; i++) {
    t3_object_add((t3_object *)G.scene, G.chars[i]->root);
    G.chars[i]->trail = trail_new(16777215, 16);
  }
  apply_sel();
  ui_load(1, "ready");
  /* e(): the menu, the title */
  G.paused = false; menu_mode(); ui_hud(false); ui_show("s-title");
#ifdef CK_AUTOSTART   /* test builds: straight into a one player fight (CK_AUTOSTART=2: two players) */
  act(CK_AUTOSTART == 2 ? "2p" : "1p"); act("start");
#endif
#ifdef CK_ONLINE_TEST /* test builds: join a private room as CARTTEST */
  snprintf(ui_online.name, sizeof ui_online.name, "CARTTEST"); snprintf(ui_online.room, sizeof ui_online.room, CK_ONLINE_TEST);
  ui_show("s-online"); online_join();
#endif
  return true;
}

void game_frame(float dt) {
  /* keys: the UI's onKey, then the window handler, as the page registers them */
  pad_keys();
  /* the UI's listeners first (ui_frame reads the keys, the typed text and the
   * pointer itself), then the window handler; a focused text field keeps the
   * keys, as the page's inputs did */
  ui_frame(dt);
  const int *codes; int n = input_key_events(&codes);
  if (!ui_online.focus) for (int i = 0; i < n; i++) app_key(codes[i]);
  if (announce_clear_at > 0 && ck_now() >= announce_clear_at) { announce_clear_at = -1; gui_clear_announce(); }

  online_update(dt);
  if (G.phase != PH_LOAD) {
    G.t += dt;
    if (!G.paused) step(dt);
  }
}

void game_draw(void) {
  if (G.arena_ready && G.phase != PH_LOAD) {
    render_scene(G.scene, G.cam);
    fx_frame(G.cam, 0.011f, 1447965, 1.05f);
    fx_begin();
    arena_fx_draw();
    trail *tr[2] = {G.f[0] ? G.f[0]->trail : NULL, G.f[1] ? G.f[1]->trail : NULL};
    gfx_draw(tr, 2);
    fx_end();
  } else render_clear(C_VOID);
  ui_draw();
  dance_draw();
}

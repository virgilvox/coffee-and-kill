/* game.c: the game (wv) and the page's startup (the async main: Rr, the
 * callbacks, the act switch and the key handler). The online host's event
 * buffer and snapshots, and the guest's replay, are here too (they are wv's). */
#include "game.h"
#include "ck.h"
#include "fighter.h"
#include "arena.h"
#include "stage.h"
#include "roster.h"
#include "weapons.h"
#include "kh/kfx.h"
#include "special.h"
#include "tourney.h"
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

/* Rt, GC, AI (the ring's radius, the page's Dv, is the stage's: stage_ring) */
#define RT 2
#define GC 60.0f
#define AI_X 2.3f

enum { PH_LOAD, PH_MENU, PH_INTRO, PH_FIGHT, PH_KO, PH_BETWEEN, PH_MATCHEND, PH_DANCE };
static const char *PH_NAMES[] = {"load", "menu", "intro", "fight", "ko", "between", "matchend", "dance"};
static int ph_index(const char *n) { for (int i = 0; i < 8; i++) if (!strcmp(PH_NAMES[i], n)) return i; return PH_MENU; }

static struct {
  t3_scene *scene;
  t3_camera *cam;
  fighter *f[2];                /* this.fighters: made for the picks, freed when no longer picked */
  trail *trail[2];
  bool arena_ready;
  int net_role;
  int phase; float phase_t;
  bool paused;
  int mode;
  ck_sel sel; ck_sel sel_backup; bool has_backup;
  int wins[2], round;
  float time_left, slow, freeze, shake, cam_yaw;
  struct { v3 pos, look; } cs;
  bool cpu[2]; int level[2];    /* who the CPU plays this match, and how well */
  char label[2][12];            /* the HUD's tags: P1, P2, CPU */
  bool has_brain[2]; brain br[2];
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
  /* the tourney: the bracket, and whether the match on now is one of its matches */
  tourney_t T; bool T_ready, in_tourney;
} G;

static fighter *other(int i) { return G.f[1 - i]; }
static const char *fname(int slot) { return G.f[slot] ? G.f[slot]->name : roster_get(roster_index(G.sel.slots[slot].ch))->name; }

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

static int humans(void) { return !G.cpu[0] + !G.cpu[1]; }
static void labels(const char **a, const char **b) {
  if (G.has_online_names) { *a = G.online_names[0]; *b = G.online_names[1]; return; }
  *a = G.label[0]; *b = G.label[1];
}
/* who plays each side: a person (on the keyboard and pads, P1 first) or the CPU at a level */
static void set_sides(bool cpu0, int lv0, const char *l0, bool cpu1, int lv1, const char *l1) {
  G.cpu[0] = cpu0; G.level[0] = lv0; G.cpu[1] = cpu1; G.level[1] = lv1;
  snprintf(G.label[0], sizeof G.label[0], "%s", l0 ? l0 : cpu0 ? "CPU" : "P1");
  snprintf(G.label[1], sizeof G.label[1], "%s", l1 ? l1 : cpu1 ? "CPU" : cpu0 ? "P1" : "P2");
}

static void place_fighters(void) {
  fighter *A = G.f[0], *P = G.f[1];
  fighter_reset(A); fighter_reset(P);
  float ax = fminf(AI_X, stage_ring() * 0.8f);
  t3_object_set_position(A->root, -ax, 0, 0); t3_object_set_position(P->root, ax, 0, 0);
  A->yaw = CK_PI / 2; P->yaw = -CK_PI / 2;
  t3_object_set_rotation(A->root, 0, A->yaw, 0); t3_object_set_rotation(P->root, 0, P->yaw, 0);
  body_teleport(A->body); body_teleport(P->body);
  trail_clear(A->trail); trail_clear(P->trail);
}

/* the picks onto the two fighters: a fighter already made for a pick is kept (or swapped to its
 * new side), one no longer picked is freed, a new pick is loaded */
static void apply_sel(void) {
  int want[2];
  for (int t = 0; t < 2; t++) { want[t] = roster_index(G.sel.slots[t].ch); if (want[t] < 0) want[t] = t; }
  if (G.f[0] && G.f[1] && G.f[0]->ri == want[1] && G.f[1]->ri == want[0] && want[0] != want[1]) {
    fighter *x = G.f[0]; G.f[0] = G.f[1]; G.f[1] = x;
  }
  for (int t = 0; t < 2; t++) {
    if (G.f[t] && G.f[t]->ri == want[t]) continue;
    if (G.f[t]) fighter_free(G.f[t]);
    G.f[t] = fighter_new(want[t]);
    body_attach(G.f[t]->body, stage_scene());
  }
  for (int t = 0; t < 2; t++) {
    fighter *e = G.f[t];
    e->slot = t; e->trail = G.trail[t];
    int w = weapon_index(G.sel.slots[t].weapon);
    if (w < 0) w = 0;
    if (e->weapon_key != w) fighter_set_weapon(e, w);
    trail_set_color(e->trail, e->W->trail);
    body_blob_shadow(e->body, !stage_shadows());
  }
  const char *a, *b; labels(&a, &b);
  gui_set_names(fname(0), fname(1), a, b);
  place_fighters();
}

/* the stage the match is on: the fighters and the effects move into its scene */
static void use_stage(int s) {
  if (stage_current() == s) return;
  stage_load(s, 1);
  for (int t = 0; t < 2; t++) if (G.f[t]) { body_attach(G.f[t]->body, stage_scene()); body_blob_shadow(G.f[t]->body, !stage_shadows()); }
}

static void menu_mode(void) {
  G.phase = PH_MENU; gui_hud(false); gui_set_hint(""); music_set_level(0.35f); place_fighters();
}

static void start_round(void);
static void start_match(void);
/* a match on the picked stage: a stage that has to load first shows the loading card meanwhile */
static int pending_stage = -1, pending_step;
static void go_match(void) {
  int s = G.sel.stage;
  if (s < 0 || s >= STAGE_N) s = 0;
  if (stage_current() == s || !stage_needs_load(s)) { use_stage(s); start_match(); return; }
  pending_stage = s; pending_step = 0;
  ui_show("s-load"); ui_load(0.1f, "walking over to the lab");
}
static bool pending_frame(void) {
  if (pending_stage < 0) return false;
  ui_load(0.1f + 0.4f * pending_step, NULL);
  if (!stage_load(pending_stage, pending_step++)) return true;
  pending_stage = -1;
  for (int t = 0; t < 2; t++) if (G.f[t]) { body_attach(G.f[t]->body, stage_scene()); body_blob_shadow(G.f[t]->body, !stage_shadows()); }
  ui_load(1, "ready");
  start_match();
  return true;
}
static void start_match(void) {
  G.wins[0] = G.wins[1] = 0; G.round = 1;
  for (int i = 0; i < 2; i++) { G.has_brain[i] = G.cpu[i] && !G.net_role; if (G.has_brain[i]) brain_init(&G.br[i], G.level[i]); }
  input_mode = G.cpu[0] || G.cpu[1] ? 1 : 2;
  gui_hide_all(); gui_hud(true);
  const char *a, *b; labels(&a, &b);
  gui_set_names(fname(0), fname(1), a, b);
  gui_set_pips(0, 0); gui_set_pips(1, 0);
  start_round();
}

static void start_round(void) {
  place_fighters();
  for (int i = 0; i < 2; i++) { G.f[i]->hp = 100; G.f[i]->meter = fminf(G.f[i]->meter, 0); }
#ifdef CK_TEST_METER   /* test builds: both start with a full EXTREME meter */
  for (int i = 0; i < 2; i++) G.f[i]->meter = 100;
#endif
  G.time_left = GC; G.slow = 1; G.freeze = 0; G.shake = 0; G.ko_winner = -1; G.perfect = false;
  ui_set_health(0, 100, true); ui_set_health(1, 100, true); ui_set_meter(0, 0); ui_set_meter(1, 0);
  ui_set_timer(GC);
  char r[24]; snprintf(r, sizeof r, "ROUND %d", G.round);
  gui_set_round(r);
  gui_combo(0, 0, NULL); gui_combo(1, 0, NULL);
  gui_set_hint(input_mode == 1 ? "WASD move   Q light   E heavy   R kick   SPACE block   SHIFT dodge   F extreme"
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

static void tourney_after_match(void);
static void finish_match(void) {
  bool A = G.wins[0] >= RT, P = G.wins[1] >= RT;
  if (G.in_tourney && A != P) { tourney_after_match(); return; }
  if (A && P) gui_set_end("DRAW", false, "NOBODY WINS. EVERYBODY BLEEDS.");
  else {
    int D = A ? 0 : 1;
    char sub[64];
    if (G.net_role) {
      if (G.has_online_names) snprintf(sub, sizeof sub, "%s TAKES THE SHOP", G.online_names[D]);
      else snprintf(sub, sizeof sub, "ONLINE VERSUS");
    } else if (G.cpu[0] && G.cpu[1]) snprintf(sub, sizeof sub, "THE CPU TAKES THE SHOP");
    else if (G.cpu[0] || G.cpu[1]) snprintf(sub, sizeof sub, !G.cpu[D] ? "THE SHOP IS YOURS" : "THE SHOP WINS THIS ONE");
    else snprintf(sub, sizeof sub, "PLAYER %d TAKES THE SHOP", D + 1);
    gui_set_end(fname(D), true, sub);
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
  const char *P0 = fname(0), *P1 = fname(1);
  dance_player e[2]; int n;
  if (G.net_role == NET_HOST) { e[0] = (dance_player){P0, DP_HUMAN, 0, 0}; e[1] = (dance_player){P1, DP_REMOTE, 0, 0}; n = 2; }
  else if (G.net_role == NET_GUEST) { e[0] = (dance_player){P0, DP_REMOTE, 0, 0}; e[1] = (dance_player){P1, DP_HUMAN, 0, 0}; n = 2; }
  else if (humans() == 2) { e[0] = (dance_player){P0, DP_HUMAN, 0, 0}; e[1] = (dance_player){P1, DP_HUMAN, 1, 0}; n = 2; }
  else if (!G.cpu[0]) { e[0] = (dance_player){P0, DP_HUMAN, 0, 0}; n = 1; }
  else { e[0] = (dance_player){P1, DP_HUMAN, 0, 0}; n = 1; }
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
  bool e = !G.net_role && humans() == 1;
  gui_announce(e ? "DANCE BREAK" : "DANCE BATTLE", e ? "GRADE TO CHARGE EXTREME. ESC SKIPS" : "HIT THE ARROWS");
  announce_clear_at = ck_now() + 1.8;
  dance_begin(G.dance_seed);
}

static void skip_dance(void) {
  if (G.phase != PH_DANCE || G.net_role || humans() != 1 || !dance_active() || G.has_dance_res) return;
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

/* a blow of a cinematic EXTREME (special.c): a hit that breaks guards, the last one knocks down */
static void special_blow(fighter *A, fighter *P, float dmg, bool last, v3 point, v3 dir) {
  if (G.phase != PH_FIGHT || P->state == ST_DEAD) return;
  P->inv = 0; P->chain = 0;
  if (P->state == ST_BLOCK) P->state = ST_IDLE;
  hit_in h = {dmg, last ? 7 : 1.5f, dir, last ? 1.1f : 0.7f, last ? REACT_DOWN : REACT_HEAVY, true};
  hit_out i = fighter_take_hit(P, &h);
  float r[3] = {point.x, point.y, point.z}, v[2] = {dir.x, dir.z};
  if (i.r != HIT_HIT && i.r != HIT_KO) return;
  A->combo++; A->combo_t = 1.3f;
  fx_ev("hit", r, NULL, v, true, i.dmg, false);
  gui_flash(last ? 0xffffff : A->W->trail, last ? 0.5f : 0.15f, last ? 240 : 110);
  G.shake = fmaxf(G.shake, last ? 1.1f : 0.5f);
  if (A->combo >= 2) gui_combo(A->slot, A->combo, A->combo >= 5 ? "ABSOLUTELY EXTREME" : A->combo >= 3 ? "NICE" : "");
  if (i.r == HIT_KO) {
    special_stop();
    if (A->state == ST_SPECIAL) { A->state = ST_IDLE; fighter_play(A, "Sword_Idle", 0.16f, false, 1, 0, true); }
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
  /* the ring: a circle (the page's Dv), or an ellipse where the stage is narrow */
  const float RX = stage_ring(), RZ = stage_ring_z();
  for (int k = 0; k < 2; k++) {
    fighter *g = G.f[k];
    v3 *q = fighter_pos(g);
    float u = q->x / RX, v = q->z / RZ, i = sqrtf(u * u + v * v);
    if (i > 1) {
      q->x /= i; q->z /= i;
      /* the outward normal of the ellipse there, and the velocity's part along it */
      float nx = q->x / (RX * RX), nz = q->z / (RZ * RZ), nl = sqrtf(nx * nx + nz * nz);
      if (nl > 0) { nx /= nl; nz /= nl; }
      float o = g->vel.x * nx + g->vel.z * nz;
      if (o > 0) { g->vel.x -= nx * o; g->vel.z -= nz * o; }
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
        if (G.match_over && G.in_tourney && G.wins[0] == G.wins[1]) { G.match_over = false; G.round++; start_round(); gui_toast("SUDDEN DEATH"); }
        else if (G.match_over) finish_match();
        else if (G.dance_on && G.net_role != NET_GUEST && humans() > 0) start_dance();
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
    bool sp = special_attacker() == P && special_time() > .5f;   /* a cinematic EXTREME swings all the way */
    if ((sp || (P->state == ST_ATTACK && e >= 0 && !P->recovering && P->cur && P->cur->time > MOVES[e].hit0 - 0.08f &&
        P->cur->time < MOVES[e].hit1 + 0.1f && !MOVES[e].foot)) && P->weapon_obj) {
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
  bool clip = true;
  if (G.phase == PH_MENU || G.phase == PH_LOAD) {
    G.menu_orbit += A * 0.12f;
    const char *scr = ui_screen();
    bool v = scr && !strcmp(scr, "s-select");
    if (scr && !strcmp(scr, "s-champ")) {   /* the champion, to the left of the card */
      float z = G.menu_orbit * 0.25f;
      w = (v3){sinf(z) * 3.6f - 1.4f, 1.35f, cosf(z) * 3.6f + 0.4f}; g = (v3){0.95f, 1.0f, 0};
      clip = false;
    } else {
      float o = v ? 6.6f : 7.4f, B = v ? 1.7f : 1.9f, z = G.menu_orbit * (v ? 0.35f : 1);
      w = (v3){sinf(z) * o * 0.9f, B, cosf(z) * o};
      float a = v ? 0 : -1.9f;
      v3 c = {cosf(z), 0, -sinf(z)};
      g = (v3){-c.x * a, (v ? 0.55f : 1.25f) - c.y * a, -c.z * a};
      if (v) { w = (v3){sinf(z) * 5.6f, 1.5f, cosf(z) * 5.6f + 0.6f}; g = (v3){0, 0.12f, 0}; }
    }
  } else if (G.phase == PH_DANCE) {
    w = (v3){0, 1.5f, 5.7f}; g = (v3){0, 1.1f, 0};
  } else {
    float vx = ep->x - pp->x, vy = ep->z - pp->z;
    float vl = sqrtf(vx * vx + vy * vy);
    if (vl < 0.2f) { vx = 1; vy = 0; vl = 1; }
    vx /= vl; vy /= vl;
    float o = atan2f(vy, vx);
    float lift, amax; bool one_side; stage_camera_rules(&lift, &amax, &one_side);
    if (one_side && cosf(o) < 0) o = ck_wrap_pi(o + CK_PI);   /* a narrow room: always from the same side */
    if (G.cam_snap) G.cam_yaw = o;
    float B = ck_wrap_pi(o - G.cam_yaw);
    G.cam_yaw += B * fminf(1, A * 3.2f);
    float zx = cosf(G.cam_yaw), zy = sinf(G.cam_yaw);
    float a = fminf(ck_clamp(3.9f + r * 0.78f, 4.8f, 8), amax), c = 1.55f + r * 0.1f + lift, Q = 1.05f;
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
  }
  float fov = 40;
#ifdef CK_TEST_TOP   /* test builds: straight down on the ring (picking a stage's spot) */
  w = (v3){0, 2.8f, 0.01f}; g = (v3){0, 0, 0}; fov = 112; clip = false; G.cam_snap = true;
#endif
  if (special_camera(&w, &g, &fov)) { G.cs.pos = w; G.cs.look = g; clip = true; }
  if (clip) stage_camera(g, &w);
  if (fabsf(fov - G.cam->fov) > 0.01f) { G.cam->fov = fov; t3_camera_update_projection_matrix(G.cam); }
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
    stage_update(A, G.t, (v3){(a->x + b->x) / 2, 1.2f, (a->z + b->z) / 2});
  }
  kfx_update(A, G.cam);
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
    if (!G.cpu[0]) { i[0] = input_poll(0); n[0] = mv_from(i[0].mx, i[0].my); }
    if (G.net_role == NET_HOST) { i[1] = remote_intent(); n[1] = mv_from(i[1].mx, i[1].my); }
    else if (!G.cpu[1]) { i[1] = input_poll(G.cpu[0] ? 0 : 1); n[1] = mv_from(i[1].mx, i[1].my); }
    for (int k = 0; k < 2; k++) if (G.has_brain[k]) {
      v2 wv; i[k] = brain_think(&G.br[k], P, G.f[k], G.f[1 - k], &wv);
      float l = sqrtf(wv.x * wv.x + wv.y * wv.y); if (l > 1) { wv.x /= l; wv.y /= l; }
      n[k] = wv;
    }
  }
  float r = hypotf(fighter_pos(e)->x - fighter_pos(t)->x, fighter_pos(e)->z - fighter_pos(t)->z);
  fight_ctx ctx = {D, r};
  /* a cinematic EXTREME holds the world still around the one who threw it */
  float rate[2] = {P, P};
  if (special_active()) {
    float ra, rw; special_rates(&ra, &rw);
    for (int k = 0; k < 2; k++) rate[k] = G.f[k] == special_attacker() ? A * ra : A * rw;
    i[0] = i[1] = (intent_t){0}; n[0] = n[1] = (v2){0, 0};
  }
  fighter_update(e, rate[0], &i[0], n[0], t, &ctx);
  fighter_update(t, rate[1], &i[1], n[1], e, &ctx);
  for (int k = 0; k < 2; k++) if (G.f[k]->special_req) {
    G.f[k]->special_req = false;
    if (!special_active() && D) {
      special_start(G.f[k], G.f[1 - k]);
      gui_announce("EXTREME", special_name(G.f[k]->W->special)); announce_clear_at = ck_now() + 1.1;
      gui_flash(G.f[k]->W->trail, 0.18f, 200);
    }
    else { G.f[k]->state = ST_IDLE; fighter_play(G.f[k], "Sword_Idle", 0.12f, false, 1, 0, true); }
  }
  if (special_active()) special_update(A);
  for (int k = 0; k < 2; k++) if (G.f[k]->state == ST_SPECIAL && special_attacker() != G.f[k]) {
    G.f[k]->state = ST_IDLE; fighter_play(G.f[k], "Sword_Idle", 0.16f, false, 1, 0, true);
  }
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
    body_animate(D->body, A);
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
      int hs = G.cpu[0] ? 1 : 0;
      fighter *w = G.f[hs];
      w->meter = (float)D.meter; ui_set_meter(hs, (float)D.meter);
      if (D.meter) snprintf(s, sizeof s, "DANCE GRADE %s  EXTREME +%d", D.grade, D.meter);
      else snprintf(s, sizeof s, "DANCE GRADE %s", D.grade);
      gui_toast(s);
    } else if (D.winner >= 0) {
      fighter *w = G.f[D.winner];
      w->meter = 70; ui_set_meter(D.winner, 70);
      snprintf(s, sizeof s, "%s STARTS CHARGED", fname(D.winner));
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
  /* a cinematic EXTREME (carts only: the page's weapons never throw one): who, which, how far in */
  if (special_active()) {
    mp_val *sp = mp_arr();
    mp_push(sp, mp_num(special_attacker() == G.f[0] ? 0 : 1)); mp_push(sp, mp_num(special_kind())); mp_push(sp, mp_num(r3(special_time())));
    mp_set(s, "sp", sp);
  }
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
  const mp_val *sp = mp_get(P, "sp");
  if (sp && sp->t == MP_ARR && sp->count >= 3) {
    int who = (int)mp_at(sp, 0)->n & 1;
    special_follow(G.f[who], G.f[1 - who], (int)mp_at(sp, 1)->n, (float)mp_at(sp, 2)->n + wt);
  } else if (special_active()) special_stop();
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
    body_animate(g->body, A);
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

void game_begin_online(int role, const char *my_name, const online_pick *me, const char *opp_name, const online_pick *opp) {
  G.sel_backup = G.sel; G.has_backup = true;
  G.net_role = role; G.mode = 1; input_mode = 1;
  G.has_remote = false;
  if (G.snap) { mp_free(G.snap); G.snap = NULL; }
  G.last_ev = 0; G.snap_n = 0;
  for (int i = 0; i < G.nev; i++) mp_free(G.evbuf[i].e);
  G.nev = 0;
  bool t = role == NET_HOST;
  /* the host is always on the left (the page's convention: slot 0 is the host) */
  const online_pick *L0 = t ? me : opp, *L1 = t ? opp : me;
  snprintf(G.sel.slots[0].ch, 12, "%s", L0->fighter); snprintf(G.sel.slots[0].weapon, 12, "%s", L0->weapon);
  snprintf(G.sel.slots[1].ch, 12, "%s", L1->fighter); snprintf(G.sel.slots[1].weapon, 12, "%s", L1->weapon);
  G.cpu[0] = G.cpu[1] = false; G.in_tourney = false;
  int st = stage_index(me->stage); if (st < 0) st = 0;
  G.has_online_names = true;
  snprintf(G.online_names[0], 16, "%s", t ? my_name : opp_name);
  snprintf(G.online_names[1], 16, "%s", t ? opp_name : my_name);
  G.has_brain[0] = G.has_brain[1] = false; G.paused = false;
  memset(G.guest_counts, 0, sizeof G.guest_counts); G.int_timer = 0;
  use_stage(st);
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
  special_stop();
  use_stage(0);
  if (G.has_backup) { G.sel = G.sel_backup; G.has_backup = false; apply_sel(); }
  menu_mode();
  /* P.onLeave: the HUD off and the online screen open, with the reason */
  ui_hud(false);
  online_leave_room_status(A);
}

/* ---------------------------------------------------------------- the tourney */

static void show_both(void) { for (int t = 0; t < 2; t++) if (G.f[t]) body_visible(G.f[t]->body, true); }
static const char *entrant_label(int e, char *buf, int cap) {
  if (G.T.e[e].ctl != TC_PLAYER) return "CPU";
  int k = 1; for (int j = 0; j < e; j++) k += G.T.e[j].ctl == TC_PLAYER;
  snprintf(buf, cap, "P%d", k); return buf;
}
/* the next match of the bracket onto the two sides, then the usual way into a match */
static void tourney_play(void) {
  tourney_t *T = &G.T;
  if (T->cur < 0) return;
  const t_match *M = &T->m[T->cur];
  const t_entrant *A = &T->e[M->a], *B = &T->e[M->b];
  snprintf(G.sel.slots[0].ch, 12, "%s", ROSTER[A->fighter].key); snprintf(G.sel.slots[0].weapon, 12, "%s", WEAPONS[A->weapon].key);
  snprintf(G.sel.slots[1].ch, 12, "%s", ROSTER[B->fighter].key); snprintf(G.sel.slots[1].weapon, 12, "%s", WEAPONS[B->weapon].key);
  char la[8], lb[8];
  set_sides(A->ctl != TC_PLAYER, A->ctl - 1, entrant_label(M->a, la, sizeof la), B->ctl != TC_PLAYER, B->ctl - 1, entrant_label(M->b, lb, sizeof lb));
  G.sel.stage = tourney_stage_of(T, T->cur, STAGE_N);
  G.in_tourney = true;
  apply_sel();
  go_match();
}
/* the champion alone in the middle of the stage, celebrating */
static void tourney_champion(void) {
  tourney_t *T = &G.T;
  const t_entrant *C = &T->e[T->champion];
  snprintf(G.sel.slots[0].ch, 12, "%s", ROSTER[C->fighter].key); snprintf(G.sel.slots[0].weapon, 12, "%s", WEAPONS[C->weapon].key);
  apply_sel();
  G.phase = PH_MENU; gui_hud(false); gui_set_hint("");
  fighter *f = G.f[0];
  fighter_reset(f);
  t3_object_set_position(f->root, 0, 0, 0); f->yaw = 0.35f; t3_object_set_rotation(f->root, 0, f->yaw, 0);
  body_teleport(f->body);
  fighter_victory(f);
  if (G.f[1]) body_visible(G.f[1]->body, false);
  G.cam_snap = true;
  ui_show("s-champ"); sfx_win(); music_set_level(0.6f);
}
static void tourney_after_match(void) {
  int side = G.wins[0] >= RT ? 0 : 1;
  tourney_report(&G.T, side, G.wins[0], G.wins[1]);
  G.in_tourney = false;
  gui_clear_announce(); gui_hud(false);
  if (G.T.cur < 0) { tourney_champion(); return; }
  G.phase = PH_MATCHEND;
  ui_show("s-bracket");
}
static bool tourney_act(const char *v) {
  if (!strcmp(v, "tourney")) {
    if (!G.T_ready) { tourney_defaults(&G.T, 8); tourney_shuffle_cpus(&G.T); G.T_ready = true; }
    G.T.started = false;
    ui_set_tourney(&G.T); ui_show("s-tsetup");
  } else if (!strcmp(v, "tstart")) {
    tourney_start(&G.T, (uint32_t)(ck_rand() * 4294967295.0));
    ui_show("s-bracket");
  } else if (!strcmp(v, "tplay")) tourney_play();
  else if (!strcmp(v, "tskip")) {
    tourney_sim(&G.T);
    if (G.T.cur < 0) tourney_champion(); else ui_menus_changed();
  } else if (!strcmp(v, "tquit")) { G.in_tourney = false; G.T.started = false; G.paused = false; special_stop(); use_stage(0); menu_mode(); show_both(); ui_show("s-title"); }
  else if (!strcmp(v, "tnew")) { G.T.started = false; G.in_tourney = false; use_stage(0); menu_mode(); show_both(); ui_show("s-tsetup"); }
  else return false;
  return true;
}

/* ---------------------------------------------------------------- the app (the page's main) */

static void act(const char *v) {
  if (!strcmp(v, "1p") || !strcmp(v, "2p")) {
    G.mode = v[0] == '1' ? 1 : 2; input_mode = G.mode;
    set_sides(false, 0, NULL, G.mode == 1, G.sel.level, NULL);
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
  } else if (!strcmp(v, "start")) go_match();
  else if (!strcmp(v, "stage")) { if (!stage_needs_load(G.sel.stage)) use_stage(G.sel.stage); ui_show("s-stage"); }
  else if (!strcmp(v, "stageback")) ui_show("s-select");
  else if (!strncmp(v, "t", 1) && tourney_act(v)) {}
  else if (!strcmp(v, "resume")) toggle_pause();
  else if (!strcmp(v, "quit")) { G.paused = false; G.in_tourney = false; special_stop(); use_stage(0); menu_mode(); show_both(); ui_show("s-title"); }
  else if (!strcmp(v, "rematch")) {
    if (G.net_role == NET_GUEST) { mp_val *c = mp_map(); mp_set(c, "k", mp_str("rm")); online_ctl(c); ui_toast("REMATCH REQUESTED"); }
    else start_match();
  } else if (!strcmp(v, "again")) {
    if (G.net_role) { game_leave_online(""); return; }
    G.paused = false; special_stop(); menu_mode(); ui_set_select(&G.sel, G.mode); ui_show("s-select");
  }
}
static void cb_weapon(int p, const char *w) { snprintf(G.sel.slots[p].weapon, 12, "%s", w); apply_sel(); }
/* a new fighter in a slot comes with its own weapon */
static void cb_fighter(int p, const char *key) {
  int ri = roster_index(key); if (ri < 0) return;
  snprintf(G.sel.slots[p].ch, 12, "%s", key);
  snprintf(G.sel.slots[p].weapon, 12, "%s", ROSTER[ri].weapon);
  apply_sel();
}
/* the stage screen shows the stage it is on when that needs no loading */
static void cb_stage(int st) {
  G.sel.stage = st;
  if (!stage_needs_load(st)) use_stage(st);
  ui_menus_changed();
}
static void cb_swap(void) {
  char a[12]; snprintf(a, sizeof a, "%s", G.sel.slots[0].ch);
  snprintf(G.sel.slots[0].ch, 12, "%s", G.sel.slots[1].ch); snprintf(G.sel.slots[1].ch, 12, "%s", a);
  apply_sel();
}
static void cb_diff(int l) { G.sel.level = l; G.level[1] = l; }

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
  snprintf(G.sel.slots[0].ch, 12, "moheeb"); snprintf(G.sel.slots[0].weapon, 12, "pipe");
  snprintf(G.sel.slots[1].ch, 12, "luis"); snprintf(G.sel.slots[1].weapon, 12, "sledge");
  G.sel.level = 1;
  /* test builds: EXTRA_CFLAGS='-DCK_PICK0="vee" -DCK_WEAPON0="arc" -DCK_PICK1=... -DCK_STAGE=3' */
#ifdef CK_PICK0
  snprintf(G.sel.slots[0].ch, 12, "%s", CK_PICK0);
#endif
#ifdef CK_PICK1
  snprintf(G.sel.slots[1].ch, 12, "%s", CK_PICK1);
#endif
#ifdef CK_WEAPON0
  snprintf(G.sel.slots[0].weapon, 12, "%s", CK_WEAPON0);
#endif
#ifdef CK_WEAPON1
  snprintf(G.sel.slots[1].weapon, 12, "%s", CK_WEAPON1);
#endif
#ifdef CK_STAGE
  G.sel.stage = CK_STAGE;
#endif
  fx_init(); gfx_init(); ui_init(); dance_init(); online_init();
  ui_cb.act = act; ui_cb.weapon = cb_weapon; ui_cb.swap = cb_swap; ui_cb.diff = cb_diff;
  ui_cb.fighter = cb_fighter; ui_cb.stage = cb_stage;
  sfx_tap = sfx_tapped;
  special_on_blow = special_blow;
  stage_init(CK_R);
  for (int i = 0; i < 2; i++) G.trail[i] = trail_new(16777215, 16);
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
    case 1: ui_load(0.35f, "building the hackerspace"); return false;
    default: break;
  }
  if (!G.arena_ready) {
    if (!stage_load(0, stage - 2)) return false;
    G.arena_ready = true;
    ui_load(0.8f, "lighting up");
    return false;
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
  if (pending_frame()) return;
  if (G.phase != PH_LOAD) {
    G.t += dt;
    if (!G.paused) step(dt);
  }
}

void game_draw(void) {
  if (G.arena_ready && G.phase != PH_LOAD) {
    render_scene(stage_scene(), G.cam);
    stage_fx_frame(G.cam);
    fx_begin();
    stage_fx_draw();
    trail *tr[2] = {G.f[0] ? G.f[0]->trail : NULL, G.f[1] ? G.f[1]->trail : NULL};
    gfx_draw(tr, 2);
    fx_end();
  } else render_clear(C_VOID);
#ifndef CK_NOUI   /* test builds: clean frames (the web page's backdrop came from one) */
  ui_draw();
#endif
  dance_draw();
}

/* special.c: see special.h. The cinematics follow KEYHOLDER's fight.c (special_start,
 * special_hit, cine_update and the special's camera in its camera code) beat for beat: the
 * same times, clips, paths and effects, with one target instead of a crowd. */
#include "special.h"
#include "fighter.h"
#include "weapons.h"
#include "audio.h"
#include "kh/kfx.h"
#include <math.h>
#include <string.h>

void (*special_on_blow)(struct fighter *att, struct fighter *def, float dmg, bool last, v3 point, v3 dir);

static const char *NAMES[SP_N] = {"GROUND POUND", "FINAL WELD", "COLD FORGE HAMMERFALL", "THOUSAND BURR CUT", "FIRST LAYER ADHESION", "FULL POWER, NO AIR ASSIST"};
const char *special_name(int sp) { return sp >= 0 && sp < SP_N ? NAMES[sp] : ""; }
bool special_is_cinematic(int sp) { return sp > SP_EXTREME && sp < SP_N; }

#define CINE_LEN 2.f
/* how many blows each lands, and how far it reaches at the start (the naginata pulls them in) */
static int spec_hits(int sp) { return sp == SP_BURR_CUT ? 7 : sp == SP_ADHESION ? 5 : 3; }
static float spec_reach(int sp) { return sp == SP_FULL_POWER ? 99 : sp == SP_ADHESION ? 7 : 8; }
/* the whole special's damage, against the page's 100 hp: about the ground pound, but sure to land */
#define SP_TOTAL 30.f

static struct {
  bool on, follow;
  fighter *P, *T;
  int sp, hits;
  float t, cy; bool held;
  v3 from, to;            /* the maul's leap */
} C;

bool special_active(void) { return C.on; }
fighter *special_attacker(void) { return C.on ? C.P : NULL; }
int special_kind(void) { return C.sp; }
float special_time(void) { return C.t; }

static v3 pos_of(fighter *f) { v3 *p = fighter_pos(f); return *p; }
static v3 chest(fighter *f) { v3 p = pos_of(f); return v3_(p.x, p.y + 1.05f, p.z); }
static v3 flat(v3 v) { return v3_(v.x, 0, v.z); }
static v3 facing(fighter *f) { return v3_(sinf(f->yaw), 0, cosf(f->yaw)); }
static float dist2d(v3 a, v3 b) { return hypotf(a.x - b.x, a.z - b.z); }
static uint32_t col(void) { return C.P->W->trail; }
static void face(fighter *f, float yaw) { f->yaw = yaw; t3_object_set_rotation(f->root, 0, yaw, 0); }
static void place(fighter *f, v3 p) { if (C.follow) return; v3 *q = fighter_pos(f); q->x = p.x; q->z = p.z; }

/* a clip on the attacker, or the nearest one its rig has (the page's rigs lack a few of KEYHOLDER's) */
static void replay(const char *clip, float speed, float fade, float seek) {
  if (C.follow) return;
  static const char *ALIAS[][2] = {{"NinjaJump_Start", "Jump_Start"}, {"Sword_Regular_Combo", "Sword_Light_Combo"},
                                   {"Spell_Simple_Shoot", "Sword_Attack_Standing"}};
  if (rig_clip_index(C.P->rig, clip) < 0)
    for (unsigned i = 0; i < sizeof ALIAS / sizeof ALIAS[0]; i++) if (!strcmp(ALIAS[i][0], clip)) { clip = ALIAS[i][1]; break; }
  ck_action *a = fighter_play(C.P, clip, fade, true, speed, seek, true);
  a->paused = false;
}
static float clip_time(void) { return C.P->cur ? C.P->cur->time : 0; }
static bool blade(v3 *b, v3 *tip) {
  if (!C.P->weapon_obj) return false;
  fighter_weapon_segment(C.P, b, tip);
  return true;
}

/* special_hit: blow n of N, the big one last */
static void hit(int n, bool last) {
  int N = spec_hits(C.sp);
  float total = SP_TOTAL * (C.sp == SP_HAMMERFALL ? 1.1f : C.sp == SP_BURR_CUT ? .95f : 1);
  float dmg = last ? total * .6f : total * .4f / (N - 1);
  uint32_t c = col();
  fighter *T = C.T;
  v3 at = T ? chest(T) : chest(C.P);
  if (T && !C.follow && T->state != ST_DEAD && special_on_blow) {
    v3 d = flat(v3_sub(pos_of(T), pos_of(C.P)));
    d = v3_len(d) > .01f ? v3_norm(d) : facing(C.P);
    special_on_blow(C.P, T, dmg, last, v3_lerp(chest(C.P), chest(T), .75f), d);
  }
  kfx_burst(at, c, last ? 1.05f : .5f);
  if (last) {
    v3 p = pos_of(C.P);
    kfx_shock(v3_(p.x, .05f, p.z), .3f, C.sp == SP_HAMMERFALL ? 9 : 7, c, .5f);
    if (!C.follow) sfx_shock();
  } else if (!C.follow) sfx_hit(C.sp != SP_BURR_CUT);
}

void special_start(fighter *P, fighter *T) {
  memset(&C, 0, sizeof C);
  C.on = true; C.P = P; C.sp = P->W->special;
  C.T = T && T->state != ST_DEAD && dist2d(pos_of(P), pos_of(T)) <= spec_reach(C.sp) ? T : NULL;
  P->vel = (v3){0, 0, 0}; P->inv = 2.3f;
  fighter_play(P, "Sword_Idle", 0.1f, false, 1, 0, true);
  sfx_special();
  kfx_burst(chest(P), col(), .55f);
}
void special_follow(fighter *P, fighter *T, int sp, float t) {
  if (!C.on || C.P != P || C.sp != sp) {
    memset(&C, 0, sizeof C);
    C.on = true; C.follow = true; C.P = P; C.T = T; C.sp = sp;
    kfx_burst(chest(P), col(), .55f);
  }
  /* catch up on the host's clock: the timeline fires what it passes */
  if (t > C.t) special_update(t - C.t);
}
void special_stop(void) {
  if (C.on && C.P && !C.follow) C.P->inv = .5f;
  C.on = false;
}

void special_rates(float *att, float *world) {
  bool after = C.hits >= spec_hits(C.sp);
  *world = after ? .32f : .05f; *att = after ? 1 / .55f : 1;
}

void special_update(float rdt) {
  if (!C.on) return;
  fighter *P = C.P, *T = C.T;
  float t0 = C.t; C.t += rdt; float t = C.t;
  int sp = C.sp, N = spec_hits(sp);
  uint32_t c = col();
  v3 d = T ? flat(v3_sub(pos_of(T), pos_of(P))) : facing(P);
  d = v3_len(d) > .01f ? v3_norm(d) : facing(P);
#define FIRE(n) do { while (C.hits < (n) && C.hits < N) { hit(C.hits, C.hits == N - 1); C.hits++; } } while (0)
#define AT(x) (t0 < (x) && t >= (x))
  switch (sp) {
  case SP_FINAL_WELD: {   /* a blink to the foe, three cuts, the last a cross of light */
    if (AT(.5f) && T) {
      v3 from = chest(P), to = v3_sub(pos_of(T), v3_scale(d, 1.65f * .6f + .25f));
      place(P, to);
      for (int i = 0; i < 4; i++) kfx_streak(v3_add(from, v3_(ck_rr(-.3f, .3f), ck_rr(-.5f, .4f), ck_rr(-.3f, .3f))), chest(P), c, .1f, .3f);
      if (!C.follow) face(P, atan2f(d.x, d.z));
      replay("Sword_Heavy_Combo", 1.65f, .05f, .15f);
      if (!C.follow) sfx_whoosh(1.2f);
      C.hits = 0;
    } else if (AT(.5f)) { replay("Sword_Heavy_Combo", 1.65f, .05f, .15f); }
    if (t >= .5f) {
      static const float ST[3] = {.42f, 1.2f, 1.82f};
      float ct = C.follow ? .15f + (t - .5f) * 1.65f : clip_time();
      while (C.hits < 3 && ct >= ST[C.hits]) { hit(C.hits, C.hits == 2); C.hits++; }
      if (C.hits == 3 && T && t < 1.5f) {   /* the weld: an X burnt into the air */
        v3 cc = chest(T), s = v3_(d.z, 0, -d.x);
        float k = ck_clamp((1.5f - t) * 2, 0, 1);
        kfx_beam(v3_add(cc, v3_add(v3_scale(s, .9f), v3_(0, .9f, 0))), v3_sub(cc, v3_add(v3_scale(s, .9f), v3_(0, .9f, 0))), c, .22f * k);
        kfx_beam(v3_add(cc, v3_sub(v3_scale(s, .9f), v3_(0, .9f, 0))), v3_sub(cc, v3_sub(v3_scale(s, .9f), v3_(0, .9f, 0))), c, .22f * k);
      }
    }
    break; }
  case SP_HAMMERFALL: {   /* up out of frame, down on them, the floor rings */
    if (AT(.5f)) {
      C.from = pos_of(P); C.to = T ? v3_sub(pos_of(T), v3_scale(d, 1.2f)) : v3_add(pos_of(P), v3_scale(d, 2));
      replay("NinjaJump_Start", 1.4f, .05f, .3f);
      kfx_dust(pos_of(P), 1, 6);
      if (!C.follow) sfx_whoosh(1.5f);
      C.hits = 0;
    }
    if (t >= .5f && t < 1.15f && !C.follow) {
      float k = (t - .5f) / .65f;
      v3 p = v3_lerp(C.from, C.to, k * k * (3 - 2 * k));
      place(P, p);
      fighter_pos(P)->y = 3.4f * sinf(fminf(k, 1) * CK_PI * .85f) * (k < .85f ? 1 : (1 - k) / .15f);
      P->vel = (v3){0, 0, 0}; face(P, atan2f(d.x, d.z));
      if (AT(.82f)) { replay("Sword_Attack", .5f, .05f, .2f); }
    }
    if (t >= .5f && t < 1.15f) { v3 b, tip; if (blade(&b, &tip)) kfx_glow(tip, .8f + (t - .5f) / .65f, c); }
    if (AT(1.15f)) {
      if (!C.follow) { fighter_pos(P)->y = 0; replay("Sword_Attack", 1.2f, 0, .36f); }
      v3 p = pos_of(P), g = v3_(p.x, .06f, p.z);
      kfx_shock(g, .2f, 8, c, .6f); kfx_shock(g, .1f, 5, 0xffffff, .35f); kfx_dust(v3_(p.x, 0, p.z), 2.2f, 14);
      for (int i = 0; i < 10; i++) { float a = i * .628f; kfx_streak(g, v3_add(g, v3_(sinf(a) * 5, ck_rr(.2f, 1.6f), cosf(a) * 5)), c, .12f, .4f); }
      if (!C.follow) sfx_shock();
    }
    if (t >= 1.15f) FIRE(1);
    if (t >= 1.4f) FIRE(2);
    if (t >= 1.62f) FIRE(3);
    break; }
  case SP_BURR_CUT: {   /* everywhere around them at once */
    static const float BT[7] = {.55f, .7f, .85f, 1.f, 1.15f, 1.3f, 1.62f};
    if (AT(.5f)) C.hits = 0;
    if (T && C.hits < 7 && t >= BT[C.hits]) {
      int n = C.hits;
      v3 from = chest(P);
      float a = atan2f(-d.x, -d.z) + n * 2.4f, r = 1.25f;
      v3 to = v3_add(pos_of(T), v3_(sinf(a) * r, 0, cosf(a) * r));
      if (n == 6) to = v3_add(pos_of(T), v3_scale(d, 1.4f));   /* the last: clean through and out the far side */
      place(P, to);
      if (!C.follow) {
        v3 dd = flat(v3_sub(pos_of(T), pos_of(P))); if (v3_len(dd) > .01f) face(P, atan2f(dd.x, dd.z));
        if (n == 6) face(P, atan2f(-d.x, -d.z) + CK_PI);
        sfx_whoosh(.8f);
      }
      kfx_streak(from, chest(P), c, .14f, .35f); kfx_streak(from, chest(P), 0xffffff, .05f, .25f);
      replay(n & 1 ? "Sword_Regular_B" : "Sword_Regular_A", 2.2f, .03f, 0);
      hit(n, n == 6); C.hits++;
      if (n == 6) {
        v3 cc = chest(T);
        for (int k = 0; k < 6; k++) { float b = k * 1.047f; kfx_streak(v3_add(cc, v3_(sinf(b) * 1.2f, cosf(b) * 1.2f, 0)), v3_sub(cc, v3_(sinf(b) * 1.2f, cosf(b) * 1.2f, 0)), c, .08f, .5f); }
      }
    }
    if (!T) FIRE(t >= 1.6f ? 7 : 0);
    break; }
  case SP_ADHESION: {   /* a turning sweep that draws them in, then the thrust */
    if (AT(.5f)) { replay("Sword_Regular_Combo", 1.6f, .05f, .98f); C.hits = 0; if (!C.follow) sfx_whoosh(1.1f); }
    if (t >= .5f && t < 1.45f) {
      if (!C.follow) {
        face(P, ck_wrap_pi(P->yaw + rdt * 15.f));
        if (clip_time() > 2.1f) { P->cur->time = 1.0f; sfx_whoosh(.9f); }
        /* the one close is pulled to the turn's edge */
        if (T) { float l = dist2d(pos_of(T), pos_of(P)); if (l < 7 && l > 1.7f) { v3 pull = v3_scale(v3_norm(flat(v3_sub(pos_of(P), pos_of(T)))), fminf(l - 1.7f, rdt * 6)); v3 *q = fighter_pos(T); q->x += pull.x; q->z += pull.z; } }
      }
      v3 g = v3_add(pos_of(P), v3_(0, .9f, 0));
      for (int k = 0; k < 3; k++) { float a = P->yaw + k * 2.094f; kfx_streak(v3_add(g, v3_(sinf(a) * 2.6f, 0, cosf(a) * 2.6f)), v3_add(g, v3_(sinf(a + .5f) * 2.6f, 0, cosf(a + .5f) * 2.6f)), c, .12f, .25f); }
    }
    static const float HT[4] = {.65f, .85f, 1.05f, 1.25f};
    while (C.hits < 4 && t >= HT[C.hits]) { hit(C.hits, false); C.hits++; }
    if (AT(1.45f)) {
      if (!C.follow) face(P, atan2f(d.x, d.z));
      replay("Sword_Dash", 1.6f, .04f, .2f);
      v3 a = chest(P), e = v3_add(a, v3_scale(d, 9));
      kfx_streak(a, e, 0xffffff, .2f, .35f); kfx_streak(a, e, c, .5f, .45f);
      if (!C.follow) sfx_whoosh(1.6f);
    }
    if (t >= 1.55f) FIRE(5);
    break; }
  default: {   /* the staff: a held beam, the crystal to the foe and past it */
    if (AT(.5f)) { replay("Spell_Simple_Shoot", 1, .05f, 0); if (!C.follow) { face(P, atan2f(d.x, d.z)); sfx_whoosh(1.2f); } C.hits = 0; }
    if (t > .62f && t < 1.5f && T) {
      v3 b, tip;
      if (!blade(&b, &tip)) tip = chest(P);
      v3 dir = v3_norm(v3_sub(chest(T), tip)), end = v3_add(tip, v3_scale(dir, 22));
      float wd = .35f * sinf(ck_clamp((t - .62f) / .88f, 0, 1) * CK_PI) + .05f;
      kfx_beam(tip, end, 0xffffff, wd * .6f); kfx_beam(tip, end, c, wd * 1.8f); kfx_glow(tip, 1.6f, c);
      kfx_glow(chest(T), 2.2f, 0xffb0c8);
      if ((int)(t * 30) % 3 == 0) kfx_motes(chest(T), c, 2, 5);
    }
    static const float BT[3] = {.7f, 1.0f, 1.35f};
    while (C.hits < 3 && t >= BT[C.hits]) { hit(C.hits, C.hits == 2); C.hits++; }
    break; }
  }
  if (t >= CINE_LEN) {
    FIRE(N);
    if (!C.follow) fighter_pos(P)->y = 0;
    special_stop();
  }
#undef FIRE
#undef AT
}

/* the special's camera: a low close look, a whip round, then the blow from the side */
bool special_camera(v3 *pos, v3 *look, float *fov) {
  if (!C.on) return false;
  fighter *P = C.P, *V = C.T ? C.T : C.P;
  float t = C.t;
  if (t < .5f || !C.held) { C.cy = P->yaw; C.held = t >= .5f; }
  if (t < .1f) C.held = false;
  float py = t < .5f ? P->yaw : C.cy;   /* after the rush the camera keeps its own angle: a spin must not spin it */
  v3 pp = pos_of(P), vp = pos_of(V);
  if (t >= .5f && C.sp == SP_HAMMERFALL && t < 1.3f) {
    v3 mid = v3_lerp(pp, vp, .5f); mid.y = vp.y;
    float a = py - 1.4f;
    *pos = v3_add(mid, v3_(sinf(a) * 6.5f, .5f, cosf(a) * 6.5f));
    *look = v3_add(v3_lerp(chest(P), chest(V), .4f), v3_(0, .3f, 0));
    *fov = 50;
  } else if (t >= .5f && C.sp == SP_ADHESION && t < 1.45f) {
    float a = py + 2.4f + (t - .5f) * .8f;
    *pos = v3_add(pp, v3_(sinf(a) * 4.6f, 2.6f, cosf(a) * 4.6f));
    *look = v3_add(pp, v3_(0, .7f, 0));
    *fov = 52;
  } else if (t < .5f) {
    float a = py + ck_lerp(.75f, -.45f, t / .5f);
    *pos = v3_add(pp, v3_(sinf(a) * 1.75f, .78f, cosf(a) * 1.75f));
    *look = v3_add(pp, v3_(0, 1.12f, 0));
    *fov = 40;
  } else {
    float k = ck_clamp((t - .5f) / .75f, 0, 1); k = k * k * (3 - 2 * k);
    v3 mid = v3_lerp(pp, vp, .5f);
    float a = py + ck_lerp(-.5f, -2.2f, k), dd = ck_lerp(3.2f, 4.4f, ck_clamp((t - 1.2f) / .7f, 0, 1));
    *pos = v3_add(mid, v3_(sinf(a) * dd, ck_lerp(.65f, 1.15f, k) + (C.sp == SP_BURR_CUT ? .9f : 0), cosf(a) * dd));
    *look = v3_add(mid, v3_(0, 1.f, 0));
    *fov = ck_lerp(44, 52, k);
  }
  return true;
}

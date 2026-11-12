/* fighter.c: see fighter.h. Y8, xC, vr and C8 of the page, line for line; comments name
 * the page's fields where they differ. The weapons' builders are in weapons.c, the
 * fighter's model in body.c. */
#include "fighter.h"
#include "kh/ksound.h"   /* a KEYHOLDER weapon swings with its own sound */
#include "geom.h"
#include "audio.h"
#include "ck.h"
#include "special.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

const char *STATE_NAMES[] = {"idle", "move", "block", "attack", "dodge", "hit", "down", "dead", "win", "special"};
int state_index(const char *n) { for (int i = 0; i < ST_N; i++) if (!strcmp(STATE_NAMES[i], n)) return i; return ST_IDLE; }

/* ---------------------------------------------------------------- moves */

const move_def MOVES[MV_N] = {
  {"L1", 'L', "Sword_Light_A", MV_L2, 4, 0.1f, 0.27f, 0.26f, 1.4f, 0.3f, 1.8f, REACT_LIGHT, 5, NULL, 0, false, false, false, 0, 0},
  {"L2", 'L', "Sword_Light_B", MV_L3, 4, 0.15f, 0.33f, 0.32f, 1.4f, 0.3f, 1.8f, REACT_LIGHT, 5, NULL, 0, false, false, false, 0, 0},
  {"L3", 'L', "Sword_Light_C", -1, 7, 0.36f, 0.56f, 0.7f, 4.5f, 0.55f, 2.2f, REACT_HEAVY, 8, "Sword_Light_C_Rec", 2.4f, false, false, false, 0, 0},
  {"H1", 'H', "Sword_Heavy_A", MV_H2, 11, 0.31f, 0.47f, 0.6f, 5.2f, 0.6f, 3, REACT_HEAVY, 11, "Sword_Heavy_A_Rec", 2.6f, true, false, false, 0, 0},
  {"H2", 'H', "Sword_Heavy_B", MV_H3, 11, 0.33f, 0.5f, 0.5f, 5.2f, 0.6f, 2.6f, REACT_HEAVY, 11, "Sword_Heavy_B_Rec", 2.6f, true, false, false, 0, 0},
  {"H3", 'H', "Sword_Heavy_C", -1, 15, 0.43f, 0.62f, 0.7f, 8, 0.95f, 3, REACT_DOWN, 14, "Sword_Heavy_C_Rec", 2.4f, true, false, false, 0, 0},
  {"KICK", 'K', "Kick", -1, 6, 0.22f, 0.37f, 0.37f, 5.5f, 0.5f, 1.2f, REACT_HEAVY, 6, NULL, 0, false, true, true, 1.35f, 0},
  {"SPECIAL", 'S', "Sword_GroundPound", -1, 26, 0.27f, 0.4f, 9, 9, 1, 0, REACT_DOWN, 0, NULL, 0, true, false, true, 1, 3.4f},
};
int move_index(const char *id) { for (int i = 0; i < MV_N; i++) if (id && !strcmp(MOVES[i].id, id)) return i; return -1; }

/* ---------------------------------------------------------------- fighter */

static const char *HIT_CLIPS[5] = {"Hit_Chest", "Hit_Head", "Hit_Stomach", "Hit_Shoulder_L", "Hit_Shoulder_R"};   /* R0 */

fighter *fighter_new(int ri, int colour) {
  fighter *f = calloc(1, sizeof *f);
  f->ri = ri; f->name = roster_get(ri)->name;
  f->body = body_new(ri, colour);
  f->root = body_root(f->body);
  f->mixer = body_mixer(f->body);
  f->rig = body_rig(f->body);
  f->hand = body_bone(f->body, BONE_RHAND);
  f->foot = body_bone(f->body, BONE_RFOOT);
  f->toe = body_bone(f->body, BONE_RTOE);
  f->hips = body_bone(f->body, BONE_HIPS);
  f->weapon_key = -1;
  f->move = -1;
  fighter_reset(f);
  return f;
}

void fighter_free(fighter *f) {
  if (!f) return;
  body_free(f->body);
  free(f);
}

void fighter_set_weapon(fighter *f, int w) {
  if (w < 0 || w >= WEAPON_N) w = 0;
  t3_object **slot[2] = {&f->weapon_obj, &f->weapon_obj2};
  for (int h = 0; h < 2; h++) if (*slot[h]) { t3_object_remove(body_grip(f->body, h), *slot[h]); *slot[h] = NULL; }
  f->weapon_key = w;
  f->W = &WEAPONS[w];
  /* the prop in the grip's frame (the page's holder under the hand, turned along kl: body.c) */
  for (int h = 0; h < f->W->hands && h < 2; h++) {
    t3_object *g = body_grip(f->body, h);
    if (!g) continue;
    t3_object *e = weapon_build(w);
    t3_object_add(g, e); t3_release(e);
    *slot[h] = e;
  }
}

void fighter_reset(fighter *f) {
  f->hp = 100; f->meter = 0; f->state = ST_IDLE; f->state_t = 0; f->inv = 0; f->move = -1;
  f->buf = 0; f->buf_t = 0; f->hit_done = false; f->chain = 0; f->combo = 0; f->combo_t = 0; f->stun = 0;
  f->vel = (v3){0, 0, 0}; f->dodge_cd = 0; f->block_hold = false; f->has_prev_seg = false; f->shown = false;
  f->sp_done = false; f->recovering = false; f->won = false; f->last_dmg = 0;
  mixer_stop_all(f->mixer);
  f->cur = NULL; f->paused = false;
  fighter_play(f, "Sword_Idle", 0, false, 1, 0, true);
}

static ck_action *act(fighter *f, const char *clip) {
  int i = rig_clip_index(f->rig, clip);
  if (i < 0) { ck_log("no clip %s", clip); i = 0; }
  return mixer_action(f->mixer, i);
}

ck_action *fighter_play(fighter *f, const char *clip, float P, bool once, float speed, float from, bool hold) {
  ck_action *g = act(f, clip);
  if (f->cur == g && !once) { g->time_scale = speed; return g; }
  f->play_count++;
  action_reset(g); g->enabled = true;
  g->loop = once ? LOOP_ONCE : LOOP_REPEAT;
  g->clamp_when_finished = hold; g->time_scale = speed; g->time = from;
  action_set_effective_weight(g, 1);
  if (f->cur && f->cur != g) action_cross_fade_from(f->mixer, g, f->cur, P);
  else action_fade_in(f->mixer, g, P);
  action_play(f->mixer, g);
  f->cur = g;
  return g;
}

v2 fighter_face(const fighter *f) { return (v2){sinf(f->yaw), cosf(f->yaw)}; }

v3 fighter_hurt_pos(const fighter *f) {
  t3_object_update_world_matrix(f->hips, true, false);
  t3_vec3 p = t3_object_get_world_position(f->hips);
  return (v3){p.x, 0, p.z};
}

bool fighter_busy(const fighter *f) {
  int s = f->state;
  return s == ST_ATTACK || s == ST_DODGE || s == ST_HIT || s == ST_DOWN || s == ST_DEAD || s == ST_WIN || s == ST_SPECIAL;
}
bool fighter_can_act(const fighter *f) { return f->state == ST_IDLE || f->state == ST_MOVE || f->state == ST_BLOCK; }
float fighter_clip_time(const fighter *f) { return f->cur ? f->cur->time : 0; }
const char *fighter_clip_name(const fighter *f) { return f->cur ? action_name(f->mixer, f->cur) : ""; }
bool fighter_vulnerable_attack(const fighter *f) {
  return f->state == ST_ATTACK && f->move >= 0 && f->cur && f->cur->time < MOVES[f->move].hit1 + 0.05f;
}

void fighter_start_move(fighter *f, int A) {
  const move_def *P = &MOVES[A];
  f->move = A; f->state = ST_ATTACK; f->state_t = 0; f->hit_done = false; f->recovering = false;
  f->sp_done = false; f->block_hold = false; f->buf = 0; f->has_prev_seg = false;
  float e = f->W->spd * (P->speed ? P->speed : 1);
  f->move_spd = e;
  if (f->cur) f->cur->paused = false;
  f->cur = fighter_play(f, P->clip, 0.05f, true, e, 0, true);
  f->cur->paused = false;
  f->clip_dur = f->rig->clips[f->cur->clip].dur;
  f->lunge_t = P->hit1 / e; f->lunge_v = P->lunge;
  f->whoosh_at = fmaxf(0, P->hit0 - 0.07f); f->whooshed = false;
  if (A == MV_SPECIAL) { f->meter = 0; sfx_special(); }
}

void fighter_start_dodge(fighter *f, float side) {
  f->state = ST_DODGE; f->state_t = 0; f->inv = 0.4f; f->dodge_cd = 0.7f; f->block_hold = false; f->move = -1; f->buf = 0;
  v2 P = fighter_face(f), e = {P.y, -P.x}, t;
  if (fabsf(side) > 0.4f) {
    t = (v2){e.x * ck_sign(side), e.y * ck_sign(side)};
    fighter_play(f, side > 0 ? "Dodge_Left" : "Dodge_Right", 0.06f, true, 2.3f, 0, true);
  } else {
    t = (v2){-P.x, -P.y};
    fighter_play(f, "Jog_Bwd_Loop", 0.06f, false, 2, 0, true);
  }
  f->vel = (v3){t.x * 6.2f, 0, t.y * 6.2f};
  f->dodge_dur = 0.46f;
  sfx_dodge();
}

void fighter_start_block(fighter *f) {
  f->state = ST_BLOCK; f->state_t = 0;
  ck_action *A = fighter_play(f, "Sword_Block", 0.07f, true, 3.2f, 0, true);
  A->paused = false;
}

hit_out fighter_take_hit(fighter *f, const hit_in *A) {
  if (f->state == ST_DEAD) return (hit_out){HIT_NONE, 0};
  if (f->inv > 0) return (hit_out){HIT_DODGED, 0};
  v2 face = fighter_face(f);
  bool t = face.x * -A->dir.x + face.y * -A->dir.z > 0.15f;   /* facing the blow */
  if (f->state == ST_BLOCK && t && !A->breaks) {
    float g = fminf(f->hp - 1, A->dmg * 0.12f);
    f->hp -= g; f->meter = fminf(100, f->meter + 4);
    f->vel = (v3){A->dir.x * A->kb * 0.55f, 0, A->dir.z * A->kb * 0.55f};
    f->play_count++;
    ck_action *i = act(f, "Sword_Block");
    action_reset(i); i->time = 0.18f; i->time_scale = 3.2f; action_play(f->mixer, i);
    f->cur = i; f->state_t = 0.1f;
    return (hit_out){HIT_BLOCKED, g};
  }
  bool D = f->state == ST_BLOCK && t && A->breaks;
  float w = A->dmg * (D ? 0.7f : 1);
  w *= fmaxf(0.45f, 1 - 0.14f * f->chain);
  f->chain++; f->combo = 0;
  f->hp = fmaxf(0, f->hp - w); f->last_dmg = w;
  f->meter = fminf(100, f->meter + w * 0.8f);
  f->buf = 0; f->move = -1; f->block_hold = false; f->recovering = false;
  f->vel = (v3){A->dir.x * A->kb, 0, A->dir.z * A->kb};
  f->state_t = 0;
  if (f->hp <= 0) {
    f->state = ST_DEAD;
    fighter_play(f, ck_rand() < 0.5f ? "Death01" : "Death02", 0.06f, true, 1, 0, true);
    f->vel.x *= 1.4f; f->vel.z *= 1.4f;
    return (hit_out){HIT_KO, w};
  }
  if (A->react == REACT_DOWN || D) {
    f->state = ST_HIT; f->stun = A->stun; f->hit_kind = REACT_DOWN;
    fighter_play(f, "Hit_Knockback", 0.05f, true, 1.1f, 0, true); f->inv = 0;
  } else if (A->react == REACT_HEAVY) {
    f->state = ST_HIT; f->stun = A->stun; f->hit_kind = REACT_HEAVY;
    fighter_play(f, "Hit_Knockback", 0.05f, true, 1.5f, 0, true);
  } else {
    f->state = ST_HIT; f->stun = A->stun; f->hit_kind = REACT_LIGHT;
    fighter_play(f, HIT_CLIPS[(int)(ck_rand() * 5)], 0.04f, true, 1.1f, 0, true);
  }
  return (hit_out){HIT_HIT, w};
}

void fighter_on_land(fighter *f, int mv) {
  f->combo++; f->combo_t = 1.3f;
  f->meter = fminf(100, f->meter + MOVES[mv].meter);
}
void fighter_victory(fighter *f) {
  f->state = ST_WIN; f->state_t = 0; f->won = true; f->vel = (v3){0, 0, 0};
  const char *wc = roster_get(f->ri)->win_clip;
  fighter_play(f, wc && rig_clip_index(f->rig, wc) >= 0 ? wc : "Celebration", 0.2f, false, 1, 0, true);
}
void fighter_intro(fighter *f) { f->state = ST_IDLE; fighter_play(f, "Sword_Idle", 0.1f, false, 1, 0, true); }

static void move_anim(fighter *f, float A, float P, v2 e) {
  float t = sqrtf(e.x * e.x + e.y * e.y);
  if (t < 0.12f) {
    if (f->state != ST_IDLE) f->state = ST_IDLE;
    fighter_play(f, "Sword_Idle", 0.18f, false, 1, 0, true);
    f->vel.x *= 0.8f; f->vel.z *= 0.8f;
    return;
  }
  f->state = ST_MOVE;
  const char *D; float w;
  if (fabsf(A) >= fabsf(P)) { D = A > 0 ? "Jog_Fwd_Loop" : "Jog_Bwd_Loop"; w = A > 0 ? 3.6f : 2.5f; }
  else { D = P > 0 ? "Jog_Left_Loop" : "Jog_Right_Loop"; w = 3; }
  fighter_play(f, D, 0.16f, false, 0.75f + 0.35f * t, 0, true);
  f->vel.x = e.x * w; f->vel.z = e.y * w;
}

/* n(): the attack the intent asks for */
static int want(const fighter *f, const intent_t *P) {
  return P->special && f->meter >= 100 ? 'S' : P->heavy ? 'H' : P->light ? 'L' : P->kick ? 'K' : 0;
}

void fighter_update(fighter *f, float A, const intent_t *P, v2 e, fighter *t, const fight_ctx *D) {
  f->state_t += A;
  if (f->inv > 0) f->inv -= A;
  if (f->dodge_cd > 0) f->dodge_cd -= A;
  if (f->combo_t > 0) { f->combo_t -= A; if (f->combo_t <= 0) f->combo = 0; }
  if (f->buf_t > 0) { f->buf_t -= A; if (f->buf_t <= 0) f->buf = 0; }
  v2 w = fighter_face(f), g = {w.y, -w.x};
  bool i = D->enabled;
  float edg = e.x * g.x + e.y * g.y;
  switch (f->state) {
    case ST_IDLE: case ST_MOVE: case ST_BLOCK: {
      if (!i) { move_anim(f, 0, 0, e); break; }
      int o = want(f, P);
      if (o == 'S' && special_is_cinematic(f->W->special)) {
        /* KEYHOLDER's EXTREME: the meter pays now, special.c takes the fighter from here */
        f->state = ST_SPECIAL; f->state_t = 0; f->meter = 0; f->move = -1; f->buf = 0; f->block_hold = false;
        f->special_req = true;
      }
      else if (o == 'S') fighter_start_move(f, MV_SPECIAL);
      else if (o == 'H') fighter_start_move(f, MV_H1);
      else if (o == 'L') fighter_start_move(f, MV_L1);
      else if (o == 'K') fighter_start_move(f, MV_KICK);
      else if (P->dodge && f->dodge_cd <= 0) fighter_start_dodge(f, edg);
      else if (P->block) {
        if (f->state != ST_BLOCK) fighter_start_block(f);
        if (f->cur && f->cur->time >= 0.36f) f->cur->time_scale = 0;
        f->vel.x = e.x * 1.4f; f->vel.z = e.y * 1.4f;
      } else {
        if (f->state == ST_BLOCK) { f->state = ST_IDLE; fighter_play(f, "Sword_Idle", 0.12f, false, 1, 0, true); }
        move_anim(f, e.x * w.x + e.y * w.y, edg, e);
      }
      break;
    }
    case ST_ATTACK: {
      const move_def *o = &MOVES[f->move];
      float B = f->cur->time;
      if (f->recovering) {
        f->rec_t += A;
        if (f->rec_t > f->rec_dur * 0.5f) {
          int z = want(f, P);
          if (z == 'L') { fighter_start_move(f, MV_L1); break; }
          if (z == 'H') { fighter_start_move(f, MV_H1); break; }
          if (z == 'K') { fighter_start_move(f, MV_KICK); break; }
          if (P->dodge && f->dodge_cd <= 0) { fighter_start_dodge(f, edg); break; }
          if (P->block) { fighter_start_block(f); break; }
        }
        if (f->rec_t >= f->rec_dur) { f->state = ST_IDLE; f->move = -1; f->recovering = false; fighter_play(f, "Sword_Idle", 0.16f, false, 1, 0, true); }
      } else {
        int z = want(f, P);
        if (z && z != 'S') { f->buf = z; f->buf_t = 0.4f; }
        if (B < o->hit1 && f->lunge_v > 0) {
          float a = 1 - B / fmaxf(0.1f, o->hit1);
          if (D->dist_to > 1.15f) { f->vel.x = w.x * f->lunge_v * a; f->vel.z = w.y * f->lunge_v * a; }
        }
        if (!f->whooshed && B >= f->whoosh_at) { f->whooshed = true; ksound_weapon(f->W->web ? -1 : f->W->kh); sfx_whoosh(f->W->dmg); }
        if (f->buf && B >= o->cancel) {
          int a = -1;
          if (f->buf == 'K') a = MV_KICK;
          else if (f->buf == 'L') a = o->type == 'L' && o->next >= 0 ? o->next : MV_L1;
          else if (f->buf == 'H') a = o->type == 'H' && o->next >= 0 ? o->next : MV_H1;
          if (a >= 0) { fighter_start_move(f, a); break; }
        }
        if (B > o->hit1 + 0.1f && f->move != MV_SPECIAL) {
          if (P->dodge && f->dodge_cd <= 0) { fighter_start_dodge(f, edg); break; }
          if (P->block) { fighter_start_block(f); break; }
        }
        if (B >= f->clip_dur - 0.02f || f->cur->paused) {
          if (o->rec) {
            f->recovering = true; f->rec_t = 0;
            f->rec_dur = f->rig->clips[rig_clip_index(f->rig, o->rec)].dur / o->rec_speed * 0.8f;
            fighter_play(f, o->rec, 0.08f, true, o->rec_speed, 0, true);
          } else { f->state = ST_IDLE; f->move = -1; fighter_play(f, "Sword_Idle", 0.14f, false, 1, 0, true); }
        }
      }
      break;
    }
    case ST_DODGE:
      if (f->state_t >= f->dodge_dur) { f->state = ST_IDLE; fighter_play(f, "Sword_Idle", 0.12f, false, 1, 0, true); }
      break;
    case ST_HIT:
      if (f->state_t >= f->stun) { f->state = ST_IDLE; f->chain = 0; fighter_play(f, "Sword_Idle", 0.14f, false, 1, 0, true); }
      if (f->hit_kind == REACT_DOWN && f->state_t > f->stun - 0.3f) f->inv = 0.3f;
      break;
    default: break;
  }
  v3 *pos = fighter_pos(f);
  pos->x += f->vel.x * A; pos->z += f->vel.z * A;
  float r = f->state == ST_ATTACK ? 7 : (f->state == ST_HIT || f->state == ST_DEAD) ? 4.5f : f->state == ST_DODGE ? 2.5f : 12;
  float v = expf(-r * A);
  f->vel.x *= v; f->vel.z *= v;
  if (f->state != ST_DEAD && f->state != ST_HIT && f->state != ST_DODGE && f->state != ST_WIN && f->state != ST_SPECIAL && t) {
    v3 *tp = fighter_pos(t);
    float o = tp->x - pos->x, B = tp->z - pos->z;
    if (o * o + B * B > 0.01f) {
      float a = ck_wrap_pi(atan2f(o, B) - f->yaw);
      f->yaw += a * fminf(1, A * (f->state == ST_ATTACK ? 6 : 14));
    }
  }
  t3_object_set_rotation(f->root, 0, f->yaw, 0);
  body_animate(f->body, A);
}

static v3 world_of(t3_object *o) { t3_vec3 p = t3_object_get_world_position(o); return (v3){p.x, p.y, p.z}; }
static v3 apply_mw(t3_object *o, float x, float y, float z) {
  t3_vec3 p = t3_vec3_apply_mat4(t3_v3(x, y, z), &o->matrix_world);
  return (v3){p.x, p.y, p.z};
}

float fighter_weapon_segment(fighter *f, v3 *A, v3 *P) {
  if (f->move >= 0 && MOVES[f->move].foot) {
    t3_object_update_world_matrix(f->foot, true, false);
    t3_object_update_world_matrix(f->toe, true, false);
    *A = world_of(f->foot); *P = world_of(f->toe);
    v3 t = {P->x - A->x, P->y - A->y, P->z - A->z};
    *P = (v3){A->x + t.x * 1.4f, A->y + t.y * 1.4f, A->z + t.z * 1.4f};
    *A = (v3){A->x - t.x * 0.2f, A->y - t.y * 0.2f, A->z - t.z * 0.2f};
    return 0.2f;
  }
  t3_object *e = f->weapon_obj;
  t3_object_update_world_matrix(e, true, false);
  *A = apply_mw(e, 0, f->W->seg0, 0);
  *P = apply_mw(e, 0, f->W->len, 0);
  return f->W->r * 0.8f;
}

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static v3 sub(v3 a, v3 b) { return (v3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static v3 madd(v3 a, v3 b, float s) { return (v3){a.x + b.x * s, a.y + b.y * s, a.z + b.z * s}; }
static v3 lerp3(v3 a, v3 b, float t) { return (v3){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

/* Yl: the closest points of segments s-A and P-e */
static float seg_seg(v3 s, v3 A, v3 P, v3 e, v3 *mid) {
  v3 t = sub(A, s), D = sub(e, P), w = sub(s, P);
  float g = dot(t, t), i = dot(D, D), n = dot(D, w), r, v;
  if (g < 1e-8f && i < 1e-8f) r = v = 0;
  else if (g < 1e-8f) { r = 0; v = clampf(n / i, 0, 1); }
  else {
    float z = dot(t, w);
    if (i < 1e-8f) { v = 0; r = clampf(-z / g, 0, 1); }
    else {
      float a = dot(t, D), c = g * i - a * a;
      r = c > 1e-8f ? clampf((a * n - z * i) / c, 0, 1) : 0;
      v = (a * r + n) / i;
      if (v < 0) { v = 0; r = clampf(-z / g, 0, 1); }
      else if (v > 1) { v = 1; r = clampf((a - z) / g, 0, 1); }
    }
  }
  v3 o = madd(s, t, r), B = madd(P, D, v);
  *mid = lerp3(o, B, 0.5f);
  v3 d = sub(o, B);
  return sqrtf(dot(d, d));
}

strike_t fighter_strike(fighter *f, fighter *A) {
  strike_t out = {0};
  if (f->state != ST_ATTACK || f->move < 0 || f->recovering || f->hit_done || A->state == ST_DEAD) return out;
  const move_def *P = &MOVES[f->move];
  float e = f->cur->time;
  if (e < P->hit0 || e > P->hit1) { f->has_prev_seg = false; return out; }
  if (P->aoe > 0) {
    if (!f->sp_done) {
      f->sp_done = true;
      v3 o = fighter_hurt_pos(f), B = fighter_hurt_pos(A);
      float z = hypotf(o.x - B.x, o.z - B.z);
      f->hit_done = true;
      out.ok = true; out.aoe = true; out.origin = o;
      if (z < P->aoe) {
        out.point = (v3){B.x, 1, B.z};
        float dx = B.x - o.x, dz = B.z - o.z, l = sqrtf(dx * dx + dz * dz);
        out.dir = l > 0 ? (v3){dx / l, 0, dz / l} : (v3){0, 0, 0};
      } else out.miss = true;
    }
    return out;
  }
  v3 t, D;
  float w = fighter_weapon_segment(f, &t, &D);
  v3 g = fighter_hurt_pos(A), i = {g.x, 0.1f, g.z}, n = {g.x, 1.62f, g.z};
  float r = 0.5f + w * 0.85f;
  v3 segs[3][2]; int ns_ = 0;
  segs[ns_][0] = t; segs[ns_][1] = D; ns_++;
  if (f->has_prev_seg) {
    for (int k = 0; k < 2; k++) {
      float o = k == 0 ? 0.33f : 0.66f;
      segs[ns_][0] = lerp3(f->prev_seg[0], t, o); segs[ns_][1] = lerp3(f->prev_seg[1], D, o); ns_++;
    }
  }
  f->prev_seg[0] = t; f->prev_seg[1] = D; f->has_prev_seg = true;
  for (int k = 0; k < ns_; k++) {
    v3 mid;
    float d = seg_seg(segs[k][0], segs[k][1], i, n, &mid);
    if (d < r) {
      v3 *pp = fighter_pos(f);
      v3 a = {g.x - pp->x, 0, g.z - pp->z};
      if (a.x * a.x + a.z * a.z < 1e-4f) a = (v3){sinf(f->yaw), 0, cosf(f->yaw)};
      float l = sqrtf(a.x * a.x + a.z * a.z); a.x /= l; a.z /= l;
      f->hit_done = true;
      out.ok = true; out.point = mid; out.dir = a;
      return out;
    }
  }
  return out;
}

/* ---------------------------------------------------------------- CPU */

const cpu_level CPU_LEVELS[3] = {
  {"ROOKIE", 0.55f, 0.22f, 0.06f, 0.55f, 0.35f, 0.4f},
  {"JOURNEYMAN", 0.32f, 0.48f, 0.16f, 0.85f, 0.6f, 0.7f},
  {"EXTREME", 0.16f, 0.7f, 0.28f, 1.2f, 0.85f, 0.95f},
};

void brain_init(brain *b, int level) {
  memset(b, 0, sizeof *b);
  b->L = &CPU_LEVELS[level < 0 ? 0 : level > 2 ? 2 : level];
  b->cool = 0.8f; b->strafe = ck_rand() < 0.5f ? 1 : -1;
}

static void brain_attack(brain *b, intent_t *A, fighter *P) {
  float e = ck_rand();
  if (P->meter >= 100 && e < 0.5f) A->special = true;
  else if (e < 0.55f) A->light = true;
  else if (e < 0.82f) A->heavy = true;
  else A->kick = true;
  b->cool = (0.7f + ck_rand() * 0.9f) / b->L->aggr;
  if (ck_rand() < 0.25f) b->retreat = 0.5f;
}

intent_t brain_think(brain *b, float A, fighter *P, fighter *e, v2 *world) {
  intent_t t = {0};
  *world = (v2){0, 0};
  b->t += A; b->cool -= A; b->strafe_t -= A; b->hold_block -= A; b->retreat -= A;
  if (P->state == ST_DEAD || e->state == ST_DEAD) return t;
  v3 D = fighter_hurt_pos(P), w = fighter_hurt_pos(e);
  v2 g = {w.x - D.x, w.z - D.z};
  float i = sqrtf(g.x * g.x + g.y * g.y);
  if (i > 1e-3f) { g.x /= i; g.y /= i; }
  v2 n = {-g.y, g.x};
  float r = 1.75f + (P->W ? P->W->len * 0.35f : 0);
  if (b->strafe_t <= 0) { b->strafe = ck_rand() < 0.5f ? 1 : -1; b->strafe_t = 0.8f + ck_rand() * 1.6f; }
  bool v = e->state == ST_ATTACK && fighter_vulnerable_attack(e) && i < r + 1.1f;
  if (v && !b->has_react) { b->has_react = true; b->react_t = b->L->react * (0.7f + ck_rand() * 0.7f); b->react_roll = ck_rand(); b->react_done = false; }
  if (!v && b->has_react && b->react_t <= 0) b->has_react = false;
  if (b->has_react) {
    b->react_t -= A;
    if (b->react_t <= 0 && !b->react_done) {
      b->react_done = true;
      if (b->react_roll < b->L->block) b->hold_block = 0.55f;
      else if (b->react_roll < b->L->block + b->L->dodge && P->dodge_cd <= 0 && fighter_can_act(P)) {
        t.dodge = true; *world = (v2){n.x * b->strafe, n.y * b->strafe};
      }
    }
    if (!v) b->has_react = false;
  }
  if (i < r + 0.7f && fighter_can_act(P) && b->hold_block <= 0 && e->state != ST_HIT && ck_rand() < A * 1.1f * b->L->block)
    b->hold_block = 0.35f + ck_rand() * 0.5f;
  if (b->hold_block > 0 && fighter_can_act(P)) { t.block = true; return t; }
  if (fighter_busy(P) || P->state == ST_DEAD) {
    if (P->state == ST_ATTACK && !P->recovering && P->hit_done && ck_rand() < A * 6 * b->L->combo) {
      if (P->move == MV_L1 || P->move == MV_L2) t.light = true;
      else if (P->move == MV_H1 || P->move == MV_H2) t.heavy = true;
    }
    return t;
  }
  bool o = (e->state == ST_ATTACK && e->recovering) || (e->state == ST_HIT && e->state_t > e->stun * 0.6f);
  if (i < r + 0.2f && (o && ck_rand() < b->L->punish * A * 10) && b->cool < 0.5f) { brain_attack(b, &t, P); return t; }
  if (i > r) {
    v2 m = {g.x + n.x * b->strafe * 0.35f, g.y + n.y * b->strafe * 0.35f};
    float l = sqrtf(m.x * m.x + m.y * m.y); if (l > 0) { m.x /= l; m.y /= l; }
    *world = m;
    if (P->meter >= 100 && i < r * 1.5f && ck_rand() < 0.02f) t.special = false;
  } else if (b->retreat > 0) {
    v2 m = {-g.x + n.x * b->strafe * 0.4f, -g.y + n.y * b->strafe * 0.4f};
    float l = sqrtf(m.x * m.x + m.y * m.y); if (l > 0) { m.x /= l; m.y /= l; }
    *world = m;
  } else if (b->cool <= 0) brain_attack(b, &t, P);
  else {
    float k = i < r * 0.7f ? -0.4f : 0.1f;
    *world = (v2){n.x * b->strafe * 0.5f + g.x * k, n.y * b->strafe * 0.5f + g.y * k};
  }
  return t;
}

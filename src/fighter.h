/* fighter.h: a fighter (Y8 of the page), its moves (xC) and the CPU brain (vr, with the
 * three levels of C8). The body (body.h) is whoever the roster entry names; the weapon
 * comes off the rack (weapons.h). */
#ifndef CK_FIGHTER_H
#define CK_FIGHTER_H
#include <stdbool.h>
#include "three.h"
#include "anim.h"
#include "input.h"
#include "ck.h"
#include "body.h"
#include "weapons.h"


/* xC: the moves */
enum { MV_L1, MV_L2, MV_L3, MV_H1, MV_H2, MV_H3, MV_KICK, MV_SPECIAL, MV_N };
enum { REACT_LIGHT, REACT_HEAVY, REACT_DOWN };
typedef struct {
  const char *id; char type; const char *clip; int next;
  float dmg, hit0, hit1, cancel, kb, stun, lunge; int react; float meter;
  const char *rec; float rec_speed; bool heavy, foot, breaks; float speed, aoe;
} move_def;
extern const move_def MOVES[MV_N];
int move_index(const char *id);             /* "L1" ... "SPECIAL", -1 if none */

/* the fighter's states */
enum { ST_IDLE, ST_MOVE, ST_BLOCK, ST_ATTACK, ST_DODGE, ST_HIT, ST_DOWN, ST_DEAD, ST_WIN,
       ST_SPECIAL };   /* a cinematic EXTREME (special.h): special.c moves the fighter */
#define ST_N 10
extern const char *STATE_NAMES[];
int state_index(const char *name);

typedef struct { float dmg, kb; v3 dir; float stun; int react; bool breaks; } hit_in;
enum { HIT_NONE, HIT_DODGED, HIT_BLOCKED, HIT_HIT, HIT_KO };
typedef struct { int r; float dmg; } hit_out;
/* strike(): what landed */
typedef struct { bool ok, miss, aoe; v3 point, dir, origin; } strike_t;

typedef struct fighter {
  int ri;                               /* the roster entry */
  const char *name;
  body *body;
  t3_object *root;                      /* the body's root: place and turn */
  const ck_rig *rig;
  ck_mixer *mixer;
  t3_object *hand, *foot, *toe, *hips;
  int weapon_key;                       /* -1 none */
  const weapon_def *W;
  t3_object *weapon_obj, *weapon_obj2;  /* the props in the right grip, and the left (paired weapons) */
  int slot;
  v3 vel; float yaw;
  ck_action *cur;
  int play_count;
  float hp, meter;
  int state; float state_t;
  float inv;
  int move;                             /* MV_* or -1 */
  int buf; float buf_t;                 /* the buffered input: 'L' 'H' 'K' 0 */
  bool hit_done, sp_done, recovering, won, paused, shown, block_hold;
  bool special_req;                     /* asked for a cinematic EXTREME this frame: game.c starts it */
  int chain, combo; float combo_t, stun, dodge_cd, last_dmg;
  int hit_kind;                         /* REACT_* of the last hit */
  float move_spd, clip_dur, lunge_t, lunge_v, whoosh_at; bool whooshed;
  float rec_t, rec_dur, dodge_dur;
  bool has_prev_seg; v3 prev_seg[2];
  /* the online guest's last snapshot clip */
  int g_play; int g_clip;
  struct trail *trail;
} fighter;

typedef struct { bool enabled; float dist_to; } fight_ctx;

fighter *fighter_new(int roster_index);
void fighter_free(fighter *f);
void fighter_set_weapon(fighter *f, int w);
void fighter_reset(fighter *f);
ck_action *fighter_play(fighter *f, const char *clip, float time, bool once, float speed, float from, bool hold);
static inline ck_action *fighter_play_d(fighter *f, const char *clip, float time) { return fighter_play(f, clip, time, false, 1, 0, true); }
v2 fighter_face(const fighter *f);
v3 fighter_hurt_pos(const fighter *f);
bool fighter_busy(const fighter *f);
bool fighter_can_act(const fighter *f);
bool fighter_vulnerable_attack(const fighter *f);
void fighter_start_move(fighter *f, int mv);
void fighter_start_dodge(fighter *f, float side);
void fighter_start_block(fighter *f);
hit_out fighter_take_hit(fighter *f, const hit_in *h);
void fighter_on_land(fighter *f, int mv);
void fighter_victory(fighter *f);
void fighter_intro(fighter *f);
void fighter_update(fighter *f, float dt, const intent_t *in, v2 mv, fighter *opp, const fight_ctx *ctx);
float fighter_weapon_segment(fighter *f, v3 *a, v3 *b);
strike_t fighter_strike(fighter *f, fighter *opp);
float fighter_clip_time(const fighter *f);
const char *fighter_clip_name(const fighter *f);
static inline v3 *fighter_pos(fighter *f) { return (v3 *)&f->root->position; }

/* vr: the CPU */
typedef struct { const char *name; float react, block, dodge, aggr, combo, punish; } cpu_level;
extern const cpu_level CPU_LEVELS[3];
typedef struct {
  const cpu_level *L;
  float t, cool; bool has_react; float react_t, react_roll; bool react_done;
  float strafe, strafe_t, hold_block, retreat;
} brain;
void brain_init(brain *b, int level);
/* think(dt, me, them): an intent and its world direction (t.world) */
intent_t brain_think(brain *b, float dt, fighter *P, fighter *e, v2 *world);

#endif

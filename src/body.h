/* body.h: a fighter's body, whichever kind (roster.h): the model in the scene, its skeleton
 * and the mixer that poses it, the bones the fight reads, and the grips weapons sit in.
 *
 *   BODY_PAGE  the web version's rigs, standard materials, as the page drew them
 *   BODY_KH    a KEYHOLDER actor (src/kh/actor.c): cel lit, outlined, with spring bones,
 *              blinking and a palette swap
 *
 * fighter.c plays clips on body_mixer() with the three.js semantics of anim.h and calls
 * body_animate() once a frame; nothing else in the fight needs to know which kind it has.
 * Bodies are made when a fighter is picked and freed when it is not (body_free), so only
 * the fighters in play are in memory. */
#ifndef CK_BODY_H
#define CK_BODY_H
#include <stdbool.h>
#include "three.h"
#include "anim.h"
#include "roster.h"

enum { BONE_HIPS, BONE_RHAND, BONE_LHAND, BONE_RFOOT, BONE_RTOE, BONE_N };

typedef struct body body;

body *body_new(int roster_index);
void body_free(body *b);
void body_attach(body *b, t3_scene *scene);       /* into this scene (NULL: out of every scene) */
int body_roster(const body *b);

t3_object *body_root(body *b);                    /* place and turn this one */
ck_mixer *body_mixer(body *b);
const ck_rig *body_rig(const body *b);
t3_object *body_bone(body *b, int which);
/* the weapon frame in a hand (0 right, 1 left): fist at the origin, +y along the weapon */
t3_object *body_grip(body *b, int hand);

/* the frame's pose: the mixer by dt, then whatever the body adds (springs, face) */
void body_animate(body *b, float dt);
/* a jump to a new place (a round start): the hair settles instead of swinging there */
void body_teleport(body *b);
void body_visible(body *b, bool on);
/* a blob shadow under the body, for stages without shadow maps */
void body_blob_shadow(body *b, bool on);

#endif

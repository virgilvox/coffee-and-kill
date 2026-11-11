/* actor.h: one anime character. A VRoid sample avatar baked by tools/prep-vrm.mjs (parsed once
 * per model and cloned, see actor_preload),
 * driven by the mocap clips (anim.c) with cel lit materials, an inverted hull
 * outline, expression morphs (a model without them keeps a still face), a procedural head look, spring bones for hair and
 * skirts, and a palette swap on top of each body's own colours.
 * Units are metres; an actor stands at about 1.6 m. yaw 0 faces +z. */
#ifndef KH_ACTOR_H
#define KH_ACTOR_H
#include "ds.h"
#include "three.h"

/* cast_meta order (tools/prep-vrm.mjs CAST). B, C, D the playable three; S long black hair, skirt, a shirt
 * over the crop top; F tall, vest and tie; H young man, messy black hair, hoodie; M man, slicked short hair,
 * vest and tie; O older man, grey hair, glasses, lines; P the M body heavier, stubble; U the H body big, dark
 * stubble; K black bob, school vest and skirt; E brown bob, yellow dress (a child); V silver hair, sci-fi suit;
 * G blonde side ponytail, white dress; W cropped hair, vest and skirt; R the W body older, grey, glasses.
 * v1.2, one body per named character (kitbashed at bake time: another sample's hair or head, painted
 * garment shells, recolours): A white bob, long black coat dress; I black twin tails, zip hoodie, jeans;
 * J sandy crop, sage sweater, jeans; L older woman, grey buns, glasses, cardigan; N a boy, short brown hair,
 * striped tee, shorts; Q long black hair, striped tee, denim overalls; T black bob, navy blazer, grey slacks;
 * X dark skin, black pixie cut, fire inspector's uniform; Y silver hair, track jacket, joggers; Z auburn side
 * tail, tee, brown baker's apron, jeans; A2 long black hair, white shirt and tie, slacks; B2 big, ginger mop,
 * plaid flannel, jeans; C2 heavy, copper hair, canvas work jacket; D2 dark skin, buzz cut, charcoal tee, cargo
 * pants; E2 long black hair, open black jacket over a red tee; F2 brown hair, glasses, tan sweater vest, khakis;
 * G2 swept blond hair, blazer, open collar. Background people (no named character wears them): H2 dark skin,
 * yellow tee; I2 brown skin, dark ponytail, striped top; J2 tan, maroon hoodie, joggers; K2 tan, twin tails,
 * orange sweater, leggings; L2 older man, grey, glasses, cardigan; M2 older woman, grey bob, red tunic; N2 big,
 * stubble, work shirt with reflective bands; O2 auburn bob, denim jacket, chinos; P2 dark hair, green polo, shorts. */
typedef enum { AM_B, AM_C, AM_D, AM_S, AM_F, AM_H, AM_M, AM_O, AM_P, AM_U, AM_K, AM_E, AM_V, AM_G, AM_W, AM_R, AM_A, AM_I, AM_J, AM_L, AM_N, AM_Q, AM_T, AM_X, AM_Y, AM_Z, AM_A2, AM_B2, AM_C2, AM_D2, AM_E2, AM_F2, AM_G2, AM_H2, AM_I2, AM_J2, AM_K2, AM_L2, AM_M2, AM_N2, AM_O2, AM_P2, AM_COUNT } actor_model;
typedef enum { EX_BLINK, EX_A, EX_I, EX_U, EX_E, EX_O, EX_JOY, EX_ANGRY, EX_SORROW, EX_FUN, EX_COUNT } actor_expr;

/* a palette swap: a hue turn (degrees) and saturation and value scales per part. With tint above
 * zero the part is dyed instead: hue is the dye's (degrees) and tint the saturation it adds, so
 * black, grey and white clothes take a colour too (a hue turn leaves them as they are) */
typedef struct { float hue, sat, val, tint; } actor_swatch;
typedef struct { actor_swatch hair, top, bottom, shoes; } actor_palette;
extern const actor_palette ACTOR_PAL_NONE;

typedef struct actor actor_t;

actor_t *actor_new(actor_model model, const actor_palette *pal, float scale, t3_scene *scene);
/* parse a model ahead of time (a loading screen, a chapter start): actor_new clones it and does
 * not stall the frame. actor_new preloads on first use otherwise. */
bool actor_preload(actor_model model);
void actor_preload_all(void);
/* level of detail, picked by the renderer by camera distance: the full body with face morphs
 * and outline within 5 m, a light body (about 5k triangles) with an outline to 10 m, the light
 * body alone beyond. bias scales those distances for every actor (2 = keep detail twice as far).
 * actor_lod_camera names the camera that renders (once; it may change its fov): the distances
 * then scale with its lens, so a long lens close up keeps the full face. */
void actor_lod_bias(float bias);
void actor_lod_camera(const t3_camera *cam);
int actor_lod_level(const actor_t *a);               /* 0 near, 1 mid, 2 far: as last rendered */
void actor_place(actor_t *a, v3 pos, float yaw);     /* a jump: the spring bones reset */
void actor_move(actor_t *a, v3 pos, float yaw);      /* a step: the spring bones follow */
v3 actor_pos(const actor_t *a);
float actor_yaw(const actor_t *a);
void actor_visible(actor_t *a, bool on);
bool actor_is_visible(const actor_t *a);
float actor_height(const actor_t *a);
float actor_hips_height(const actor_t *a);

/* crossfade to a clip by name (tools/prep-vrm.mjs CLIPS); replaying the current clip only
 * changes its speed unless restart is set */
void actor_play(actor_t *a, const char *clip, float speed, bool loop, float fade);
void actor_replay(actor_t *a, const char *clip, float speed, bool loop, float fade);
bool actor_clip_done(const actor_t *a);              /* a one shot reached its end */
float actor_clip_time(const actor_t *a);             /* seconds into the current clip */
float actor_clip_len(const actor_t *a);
float actor_clip_ground_speed(const actor_t *a);     /* metres a second the clip's feet travel at speed 1 */
void actor_clip_seek(actor_t *a, float t);           /* jump the current clip to t seconds: sections of a long mocap take */

/* look at a world point (head and neck turn, clamped); amount 0 lets go */
void actor_look(actor_t *a, v3 target, float amount);
/* expressions ease to these amounts, 0..1; blink runs on its own */
void actor_expr_set(actor_t *a, actor_expr e, float amount);
void actor_expr_clear(actor_t *a);
/* lip sync: mouth open 0..1, shape chooses the vowel; 0 closes */
void actor_mouth(actor_t *a, float open, float shape);

void actor_update(actor_t *a, float dt);             /* after actor_move / actor_play, every frame */

/* attach points: a group that follows the bone, in the bone's frame, x along the fingers
 * for the right hand; add a prop with t3_object_add. */
t3_object *actor_socket_rhand(actor_t *a);
t3_object *actor_socket_lhand(actor_t *a);
v3 actor_bone_pos(actor_t *a, const char *bone);     /* world position, after actor_update */
v3 actor_head_pos(actor_t *a);
/* a flat colour flash over the whole model (hit flash): colour multiplier 0..n, 1 = normal */
void actor_flash(actor_t *a, float r, float g, float b);
/* the blob shadow is drawn by the actor at floor_y */
void actor_set_floor(actor_t *a, float floor_y);
/* a new palette on a model already made (reuses the actor, so no glTF parse); uploads the atlas again */
void actor_repalette(actor_t *a, const actor_palette *pal);
/* both forearms up, open handed, over whatever clip plays: 0..1, eases (a beaten fighter yielding) */
void actor_hands_up(actor_t *a, float amount);
/* both hands onto world points (right, left) over the clip, the arms bent to reach: 0..1, eases (the bike's grips) */
void actor_reach(actor_t *a, v3 right, v3 left, float amount);


/* this game's additions (see actor.c) */
#include "anim.h"
void actor_attach(actor_t *a, t3_scene *scene);          /* into this scene (NULL: out of any) */
void actor_free(actor_t *a);                             /* the model unloads with its last actor */
ck_mixer *actor_mixer(actor_t *a);
int actor_model_find(const char *letter);                 /* "C", "E2": the AM_ index, -1 if none */                       /* the fighter drives it; actor_update runs it */
const ck_rig *actor_rig(void);
t3_object *actor_root(actor_t *a);
t3_object *actor_bone(actor_t *a, const char *name);
void actor_shadow(actor_t *a, bool on);
#endif

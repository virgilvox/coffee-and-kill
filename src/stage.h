/* stage.h: where the fight happens. The web version's workshop (arena.c) and KEYHOLDER's
 * places (src/kh/world.c): the lab, Main Street, the back lot and the five Battle Zone
 * arenas. Each stage is one entry in STAGES (stage.c).
 *
 * Every stage is seen in the game's own coordinates: the ring's centre at the origin and its
 * floor at y 0, the fighters starting along x. A KEYHOLDER place is moved there (its world sits
 * under one group, turned and shifted), so the fight, the camera and the effects need no
 * offsets. Each stage has its own scene and its own look (tone mapping, shadows, fog); the
 * fighters and the effects move into the current one.
 *
 * Only the workshop is built at boot. KEYHOLDER's world loads the first time one of its
 * stages is picked and then stays (its stages switch without loading again).
 *
 * To add a stage: an entry in STAGES. A KEYHOLDER arena or a named place of its town works
 * as it is; a new kind of place needs a backend in stage.c. */
#ifndef CK_STAGE_H
#define CK_STAGE_H
#include <stdbool.h>
#include "three.h"
#include "ck.h"

enum { STK_WORKSHOP, STK_KH_ARENA, STK_KH_TOWN, STK_KH_LAB };

typedef struct {
  const char *key, *name, *blurb;
  int kind;              /* STK_* */
  bool web;              /* the web version's own arena: the only one online against the web page */
  float ring;            /* how far from the centre the fighters may go */
  int kh_stage;          /* KEYHOLDER's world_stage for arenas */
  const char *place;     /* STK_KH_TOWN: a named place (world_place) */
  float lab_x, lab_z;    /* STK_KH_LAB: a point on the HSL-108 plan */
  float yaw;             /* the fighters' line, turned this much from the place's x axis */
  float hour;            /* the town's clock (lights, sky) */
} stage_def;

extern const stage_def STAGES[];
extern const int STAGE_N;
int stage_index(const char *key);             /* -1 if unknown */

void stage_init(t3_renderer *r);
/* make stage s the current one; returns false while it is still loading (call again next frame,
 * with step counting up from 0) and true when it is current */
bool stage_load(int s, int step);
bool stage_needs_load(int s);                 /* true when picking s would load (show a loading card) */
int stage_current(void);
t3_scene *stage_scene(void);
float stage_ring(void);

/* per frame: the stage's animation, with the point the fight is about (the robot eye and the
 * town's culling follow it) */
void stage_update(float dt, float t, v3 focus);
/* the effects the stage draws itself in the fx pass, and the fog and exposure that pass uses */
void stage_fx_draw(void);
void stage_fx_frame(t3_camera *cam);
/* keep a camera at pos looking at look out of the walls */
void stage_camera(v3 look, v3 *pos);
/* whether the stage has shadow maps (else the page's fighters get a blob) */
bool stage_shadows(void);

#endif

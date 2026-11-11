/* world.h: the walkable world. Downtown Mesa at real scale around HeatSync Labs, the
 * lab itself (HSL-108 model) and the fight arenas. Units are metres, y is up.
 *
 * World axes: +x is east, -z is north. The origin is on the lab's front wall at its door,
 * on the sidewalk's level. West Main Street runs along x south of the origin; the lab runs
 * north from it (its back wall near z = -40). OpenStreetMap is the source for where
 * everything is: even numbers on the north side of Main, odd on the south. */
#ifndef KH_WORLD_H
#define KH_WORLD_H
#include "ds.h"
#include "three.h"

typedef enum {
  STAGE_TOWN,          /* the city and the lab, one continuous space */
  STAGE_ARENA_BENCH,   /* "The Long Bench": the lab's hall opened out, benches around a wide floor */
  STAGE_ARENA_LASER,   /* "Laser Corner": a ring under an oversized laser gantry */
  STAGE_ARENA_FRIDGE,  /* "The Fridge": the kitchen, grown, tiled and frosted */
  STAGE_ARENA_SHOP,    /* "Machine Shop": lathes and mills as pillars around a steel floor */
  STAGE_ARENA_UNFOLD,  /* "The Whole Lab, Unfolded": every room of the lab around a huge disc, night sky */
  STAGE_COUNT
} world_stage;

void world_init(t3_scene *scene);
void world_set_stage(world_stage s);
world_stage world_get_stage(void);
/* per frame: which chunks draw (focus: the camera or the player) */
void world_update(float dt, v3 focus);
/* lab clock, in hours (9.0 opening .. 24.0 lock up): sun, sky, fog, street lamps, neon, the lab's lights */
void world_set_time(float hours);
/* a stage's fighting floor: centre on the floor and a safe radius */
void world_stage_floor(world_stage s, v3 *center, float *radius);

/* a standing cylinder (feet at p->y) moved by vel*dt: slides along walls and furniture, lands on
 * floors and tops (*grounded, zeroes vel->y), steps up ledges under WORLD_STEP */
#define WORLD_STEP 0.32f
void world_move(v3 *p, v3 *vel, float r, float h, float dt, bool *grounded);
bool world_raycast(v3 origin, v3 dir, float max_dist, float *t, v3 *normal);
bool world_visible(v3 a, v3 b);
float world_floor_y(v3 p);                     /* the first surface at or below p, or -1e9 */
bool world_in_lab(v3 p);
/* v1.1: who the frame is about (the player in a roam, the people in a cutscene). The lab's lights, its
 * interior and the front door follow the subject, not the lens: a lens outside the glass looking in at
 * someone inside still sees the lab. Call it each frame before world_update; it lapses after half a
 * second, and then the focus passed to world_update stands in. */
void world_subject(v3 p);
/* v1.1: a camera probe. How far from `from` toward `to` a sphere of radius r can travel before it
 * touches a wall, a prop or the ground (rays at its centre and its rim, then a sphere test in the lab).
 * Pick r so the sphere covers the near plane's corners. Returns a distance, 0..|to - from|. */
float world_cam_clear(v3 from, v3 to, float r);
float world_cam_clear_all(v3 from, v3 to, float r);   /* the same, furniture and shelves included */
/* v1.1: the lab's seats, from the HSL-108 model's chairs, stools and sofa (i from 0 until it returns false):
 * the middle of the seat on the floor (pos.y is the seat's height), the yaw a sitter faces, and soft for the
 * sofa and the lounge chair */
bool world_seat(int i, v3 *pos, float *yaw, bool *soft);

/* the HSL-108 plan (metres: x from the street to the back, z across from the west wall) to world */
v3 world_lab(float plan_x, float plan_z);
/* lab plan yaw (degrees, 0 faces plan +z, 90 faces plan +x, as DOLLY's marks) to world yaw (radians) */
float world_lab_yaw(float plan_deg);

/* named places: "lab_door", "nile", "worth", "proof", "level1", "center_main_station", ... see
 * docs/DESIGN.md. Standing point on the sidewalk in front, and the yaw that faces the place. */
bool world_place(const char *name, v3 *pos, float *yaw);
/* the door's open amount, 0..1, driven by the director. Calling it turns the automatic door off */
void world_door(float open);
/* the front door opens by itself when the focus comes within 3 m (the default until world_door is called) */
void world_door_auto(bool on);
/* chunks drawn and their triangles before frustum culling, for the debug overlay */
void world_debug_counts(int *draws, int *tris);
/* v1.1: an arena's second look for a boss's phase two (0 normal, 1 transformed: the laser corner fogs green,
 * the fridge frosts over, the shop goes back to the old building, the unfolded lab breaks apart). */
void world_arena_phase(int phase);
/* v1.1: the light rail rings its bell (coming into Center and Main, leaving it, passing near the focus).
 * Poll once a frame: true once per ring, with where it rang. Audio has no bell yet. */
bool world_train_bell(v3 *at);
/* v1.1: cars in Main's through lanes (on by default). They wait short of the focus when it is on their half of
 * the road; a shot that frames people across the street can turn them off */
void world_traffic(bool on);
#endif

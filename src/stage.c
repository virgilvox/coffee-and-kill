/* stage.c: see stage.h. Two backends: the workshop (arena.c, the page's Z0, with the page's
 * look: ACES at 1.05, PCF shadows, FogExp2) and KEYHOLDER's world (src/kh/world.c, with its
 * look: no tone mapping, a hemisphere and a sun, linear fog, blob shadows). */
#include "stage.h"
#include "arena.h"
#include "fx.h"
#include "kh/world.h"
#include "kh/kfx.h"
#include <string.h>
#include <math.h>

#define WORKSHOP(...) .kind = STK_WORKSHOP, .web = true, .ring = 4.45f, __VA_ARGS__
#define ARENA(st) .kind = STK_KH_ARENA, .ring = 4.45f, .kh_stage = st
#define TOWN(pl, hr) .kind = STK_KH_TOWN, .ring = 4.45f, .place = pl, .hour = hr
const stage_def STAGES[] = {
  {"workshop", "THE WORKSHOP", "Robot arms, cogs and an Arduino Mega the size of a wall.", WORKSHOP(.cam_clear = 0)},
  /* the lab is narrow and full: the fight runs down the open floor by the electronics bench, the
   * ring is an ellipse that keeps off the tables, and the lens sits up over them */
#ifdef CK_TEST_LABX
  {"lab", "THE LAB", "test", .kind = STK_KH_LAB, .ring = 3.2f, .ring_z = 0.9f, .lab_x = CK_TEST_LABX, .lab_z = 4.7f, .yaw = -1.5708f, .hour = 15.5f, .cam_lift = 0.75f, .cam_max = 4.6f, .cam_clear = 1, .cam_side = true},
#else
  {"lab", "THE LAB", "HSL-108 on a weekday afternoon, down the open floor by the electronics bench.", .kind = STK_KH_LAB, .ring = 3.2f, .ring_z = 0.9f, .lab_x = 25, .lab_z = 4.7f, .yaw = -1.5708f, .hour = 15.5f, .cam_lift = 0.75f, .cam_max = 4.6f, .cam_clear = 1, .cam_side = true},
#endif
  {"main", "MAIN STREET", "West Main in front of the lab, the light rail going by.", TOWN("mid_block", 12.5f), .cam_clear = 2},
  {"lot", "THE BACK LOT", "Behind the lab. Plenty of daylight, plenty of gravel.", TOWN("lot_fight", 17.6f), .cam_clear = 2},
  {"bench", "THE LONG BENCH", "The lab's hall opened out, benches around a wide floor.", ARENA(STAGE_ARENA_BENCH)},
  {"laser", "LASER CORNER", "A ring under an oversized laser gantry.", ARENA(STAGE_ARENA_LASER)},
  {"fridge", "THE FRIDGE", "The kitchen, grown, tiled and frosted.", ARENA(STAGE_ARENA_FRIDGE)},
  {"shop", "MACHINE SHOP", "Lathes and mills as pillars around a steel floor.", ARENA(STAGE_ARENA_SHOP)},
  {"unfold", "THE WHOLE LAB, UNFOLDED", "Every room of the lab around a huge disc, night sky.", ARENA(STAGE_ARENA_UNFOLD)},
};
const int STAGE_N = (int)(sizeof STAGES / sizeof STAGES[0]);
int stage_index(const char *k) { for (int i = 0; k && i < STAGE_N; i++) if (!strcmp(STAGES[i].key, k)) return i; return -1; }

static t3_renderer *R;
static int cur = -1;
static t3_scene *ws_scene;            /* the workshop */
static bool ws_ready;
static t3_scene *kh_scene;            /* KEYHOLDER's world */
static t3_object *kh_off;             /* its world under one group: turned and shifted onto the ring */
static bool kh_ready;
static v3 kh_c; static float kh_yaw;  /* the current place in KEYHOLDER's coordinates */

void stage_init(t3_renderer *r) {
  R = r;
  ws_scene = t3_scene_new();
  arena_scene_setup(r, ws_scene);
}

int stage_current(void) { return cur; }
t3_scene *stage_scene(void) { return cur >= 0 && STAGES[cur].kind != STK_WORKSHOP ? kh_scene : ws_scene; }
float stage_ring(void) { return cur >= 0 ? STAGES[cur].ring : 4.45f; }
float stage_ring_z(void) { return cur >= 0 && STAGES[cur].ring_z > 0 ? STAGES[cur].ring_z : stage_ring(); }
void stage_camera_rules(float *lift, float *max_dist, bool *one_side) {
  *lift = cur >= 0 ? STAGES[cur].cam_lift : 0;
  *one_side = cur >= 0 && STAGES[cur].cam_side;
  *max_dist = cur >= 0 && STAGES[cur].cam_max > 0 ? STAGES[cur].cam_max : 99;
}
bool stage_shadows(void) { return cur < 0 || STAGES[cur].kind == STK_WORKSHOP; }
bool stage_needs_load(int s) { return s >= 0 && s < STAGE_N && (STAGES[s].kind == STK_WORKSHOP ? !ws_ready : !kh_ready); }

/* KEYHOLDER's world: everything but the two lights goes under kh_off (the sun's direction is its
 * position against the origin, so the lights stay where they are) */
static void kh_build(void) {
  kh_scene = t3_scene_new();
  world_init(kh_scene);
  kh_off = t3_group_new();
  t3_object *root = &kh_scene->base;
  t3_object *move[64]; int n = 0;
  for (int i = 0; i < root->child_count && n < 64; i++) {
    t3_object *o = root->children[i];
    if (o->type == T3_HEMISPHERE_LIGHT || o->type == T3_DIRECTIONAL_LIGHT) continue;
    move[n++] = o;
  }
  for (int i = 0; i < n; i++) { t3_retain(move[i]); t3_object_remove(root, move[i]); t3_object_add(kh_off, move[i]); t3_release(move[i]); }
  t3_object_add(root, kh_off);
  kh_ready = true;
}

static void kh_place(const stage_def *d) {
  v3 c = {0, 0, 0}; float r = 4;
  if (d->kind == STK_KH_ARENA) { world_set_stage((world_stage)d->kh_stage); world_stage_floor((world_stage)d->kh_stage, &c, &r); }
  else {
    world_set_stage(STAGE_TOWN);
    if (d->kind == STK_KH_LAB) c = world_lab(d->lab_x, d->lab_z);
    else if (!world_place(d->place, &c, NULL)) world_stage_floor(STAGE_TOWN, &c, NULL);
    float fy = world_floor_y(v3_(c.x, c.y + 1.5f, c.z));
    if (fy > -1e8f) c.y = fy;
    world_set_time(d->hour);
  }
  kh_c = c; kh_yaw = d->yaw;
  /* game = R(-yaw) (world - c): the group holds that transform */
  t3_object_set_rotation(kh_off, 0, -kh_yaw, 0);
  float cs = cosf(-kh_yaw), sn = sinf(-kh_yaw);
  t3_object_set_position(kh_off, -(c.x * cs + c.z * sn), -c.y, -(-c.x * sn + c.z * cs));
  t3_object_update_matrix_world(kh_off, true);
}
static v3 kh_world(v3 g) {   /* a game point in KEYHOLDER's coordinates */
  float cs = cosf(kh_yaw), sn = sinf(kh_yaw);
  return (v3){kh_c.x + g.x * cs + g.z * sn, kh_c.y + g.y, kh_c.z - g.x * sn + g.z * cs};
}

static void look(int s) {
  if (STAGES[s].kind == STK_WORKSHOP) {
    t3_renderer_set_tone_mapping(R, T3_ACES_FILMIC_TONE_MAPPING, 1.05f);
#ifndef CK_PROF_NOSHADOW
    t3_renderer_set_shadow_map(R, true, T3_PCF_SHADOW_MAP);
#endif
  } else {
    t3_renderer_set_tone_mapping(R, T3_NO_TONE_MAPPING, 1);
    t3_renderer_set_shadow_map(R, false, T3_PCF_SHADOW_MAP);
  }
}

bool stage_load(int s, int step) {
  if (s < 0 || s >= STAGE_N) s = 0;
  const stage_def *d = &STAGES[s];
  if (d->kind == STK_WORKSHOP) {
    if (!ws_ready) { if (!arena_build(ws_scene, step)) return false; ws_ready = true; }
  } else {
    if (!kh_ready) { if (step == 0) return false; kh_build(); }
    kh_place(d);
  }
  cur = s;
  look(s);
  kfx_attach(stage_scene());
  return true;
}

void stage_update(float dt, float t, v3 focus) {
  if (cur < 0) return;
  if (STAGES[cur].kind == STK_WORKSHOP) {
    arena_set_focus(focus.x, 1.2f, focus.z);
    arena_update(dt, t);
    return;
  }
  v3 w = kh_world(focus);
  world_subject(w);
  world_update(dt, w);
}

void stage_fx_draw(void) { if (cur >= 0 && STAGES[cur].kind == STK_WORKSHOP) arena_fx_draw(); }

void stage_fx_frame(t3_camera *cam) {
  /* the workshop's FogExp2(1447965, .011) at exposure 1.05; KEYHOLDER's stages have neither */
  if (cur < 0 || STAGES[cur].kind == STK_WORKSHOP) fx_frame(cam, 0.011f, 1447965, 1.05f);
  else fx_frame(cam, 0, 0, 0);
}

void stage_camera(v3 look_at, v3 *pos) {
  if (cur < 0) return;
  const stage_def *d = &STAGES[cur];
  if (d->kind == STK_WORKSHOP) {
    pos->x = ck_clamp(pos->x, -ROOM_HX + 1, ROOM_HX - 1); pos->z = ck_clamp(pos->z, -ROOM_HZ + 1, ROOM_HZ - 1);
    return;
  }
  if (!d->cam_clear) return;
  /* pull the lens in front of the walls (1) or of anything (2) between it and the fight */
  v3 a = kh_world(look_at), b = kh_world(*pos);
  float full = v3_len(v3_sub(b, a));
  if (full < 1e-3f) return;
  float k = (d->cam_clear == 1 ? world_cam_clear(a, b, 0.22f) : world_cam_clear_all(a, b, 0.22f)) / full;
  if (k < 1) {
    k = fmaxf(k, 0.35f);
    pos->x = look_at.x + (pos->x - look_at.x) * k;
    pos->y = look_at.y + (pos->y - look_at.y) * k;
    pos->z = look_at.z + (pos->z - look_at.z) * k;
  }
}

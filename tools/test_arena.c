/* test_arena.c: a driver for the arena alone. Builds it through the boot
 * stages, then holds fixed camera poses (POSE env is not available in a cart,
 * so the pose steps every 90 frames after boot):
 *   0 the title's menu orbit (menuOrbit 0.3), 1 the fight camera at the round
 *   start, 2 a high overview, 3 the select screen's orbit */
#include "game.h"
#include "ck.h"
#include "arena.h"
#include "fx.h"
#include "overlay.h"
#include "fighter.h"
#include <math.h>

static t3_scene *scene; static t3_camera *cam;
static float T; static int frames;
static fighter *fl, *fm; static t3_object *spare[2];

void game_init(void) {
  fx_init();
  scene = t3_scene_new();
  cam = t3_perspective_camera_new(40, 1280.f / 720.f, 0.1f, 80);
  arena_scene_setup(CK_R, scene);
}
bool game_boot(int stage) {
  if (arena_build(scene, stage) != 1) return false;
  /* the two fighters with weapons, and the other two weapons on the floor, so
   * their program states under the arena's lights are captured */
  fl = fighter_new("a", "LUIS", 1.04f, "char_a.glb", "rig_a.bin");
  fm = fighter_new("b", "MOHEEB", 0.97f, "char_b.glb", "rig_b.bin");
  fighter_set_weapon(fm, 0); fighter_set_weapon(fl, 2);
  t3_object_set_position(fm->root, -2.3f, 0, 0); fm->yaw = CK_PI / 2; t3_object_set_rotation(fm->root, 0, fm->yaw, 0);
  t3_object_set_position(fl->root, 2.3f, 0, 0); fl->yaw = -CK_PI / 2; t3_object_set_rotation(fl->root, 0, fl->yaw, 0);
  t3_object_add((t3_object *)scene, fm->root); t3_object_add((t3_object *)scene, fl->root);
  for (int k = 0; k < 2; k++) {
    spare[k] = weapon_build(k == 0 ? 1 : 3);
    t3_object_set_position(spare[k], -0.6f + k * 1.2f, 0.05f, 1.2f); t3_object_set_rotation(spare[k], CK_PI / 2, 0, 0.3f);
    t3_object_add((t3_object *)scene, spare[k]);
  }
  return true;
}

static void pose(int k) {
  float px, py, pz, lx, ly, lz;
  if (k == 0 || k == 3) {
    bool sel = k == 3;
    float z = 0.3f, o = sel ? 6.6f : 7.4f, B = sel ? 1.7f : 1.9f;
    px = sinf(z) * o * 0.9f; py = B; pz = cosf(z) * o;
    float a = sel ? 0 : -1.9f, cx = cosf(z), cz = -sinf(z);
    lx = -a * cx; ly = sel ? 0.55f : 1.25f; lz = -a * cz;
    if (sel) { px = sinf(z) * 5.6f; py = 1.5f; pz = cosf(z) * 5.6f + 0.6f; lx = 0; ly = 0.35f; lz = 0; }
  } else if (k == 1) { px = 0; py = 2.01f; pz = 7.488f; lx = 0; ly = 1.05f; lz = 0; }
  else if (k == 2) { px = 0; py = 7; pz = 9; lx = 0; ly = 1; lz = -2; }
  else if (k == 4) { px = 5; py = 4; pz = 2; lx = -13; ly = 4; lz = 0; }
  else { px = 6; py = 5; pz = 6; lx = -9; ly = 6; lz = -8; }
  t3_object_set_position(cam, px, py, pz);
  t3_object_look_at(cam, lx, ly, lz);
}

void game_frame(float dt) {
  T += dt; frames++;
  arena_set_focus(0, 1.2f, 0);
  arena_update(dt, T);
  if (fm) { mixer_update(&fm->mixer, dt); mixer_update(&fl->mixer, dt); }
  pose((frames / 90) % 6);
}
void game_draw(void) {
  render_scene(scene, cam);
  fx_frame(cam, 0.011f, 1447965, 1.05f);
  fx_begin(); arena_fx_draw(); fx_end();
  ov_begin(); ov_vignette(); ov_grain(T); ov_end();
}

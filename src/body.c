/* body.c: see body.h. The page bodies are fighter_new of the old fighter.c (the page's Y8
 * constructor: its glb, its baked clips, its material tweaks and the kl grip axis); the
 * KEYHOLDER bodies are actors from src/kh/actor.c with the fighter driving their mixer. */
#include "body.h"
#include "ck.h"
#include "kh/actor.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct body {
  int ri;
  const roster_entry *e;
  t3_object *root;                /* the page body: a group holding the scaled model; KEYHOLDER: the actor's root */
  t3_object *bone[BONE_N], *grip[2];
  ck_mixer *mixer;
  /* the page body */
  t3_gltf *gltf; ck_mixer own; t3_mesh *blob; t3_scene *scene;
  /* KEYHOLDER */
  actor_t *actor;
};

/* the page's rigs, loaded once each (a few MB of clips) */
static struct { const char *name; ck_rig *rig; } RIGS[4];
static const ck_rig *page_rig(const char *name) {
  for (int i = 0; i < 4; i++) if (RIGS[i].name && !strcmp(RIGS[i].name, name)) return RIGS[i].rig;
  for (int i = 0; i < 4; i++) if (!RIGS[i].name) { RIGS[i].name = name; RIGS[i].rig = rig_load(name); return RIGS[i].rig; }
  return rig_load(name);
}

/* Quaternion.setFromUnitVectors */
static t3_quat from_unit_vectors(t3_vec3 a, t3_vec3 b) {
  t3_quat q; float r = a.x * b.x + a.y * b.y + a.z * b.z + 1;
  if (r < 1e-8f) {
    r = 0;
    if (fabsf(a.x) > fabsf(a.z)) { q.x = -a.y; q.y = a.x; q.z = 0; q.w = r; }
    else { q.x = 0; q.y = -a.z; q.z = a.y; q.w = r; }
  } else {
    q.x = a.y * b.z - a.z * b.y; q.y = a.z * b.x - a.x * b.z; q.z = a.x * b.y - a.y * b.x; q.w = r;
  }
  return t3_quat_normalize(q);
}

static void mesh_setup(t3_object *o, void *ctx) {
  (void)ctx;
  if (o->type != T3_MESH && o->type != T3_SKINNED_MESH) return;
  o->frustum_culled = false; o->cast_shadow = true; o->receive_shadow = true;
  t3_mesh *m = (t3_mesh *)o;
  for (int i = 0; i < m->material_count; i++) {
    t3_material *D = m->materials[i];
    if (!D) continue;
    D->emissive_intensity = 0.1f; D->metalness = 0.05f; D->roughness = 0.82f;
    if (D->map) D->map->anisotropy = 8;
    D->version++;
  }
}

/* the blob shadow of a page body on a stage without shadow maps (KEYHOLDER's, in a page body's frame) */
static t3_mesh *blob_new(void) {
  static t3_material *M; static t3_geometry *G;
  if (!M) {
    enum { N = 32 };
    uint8_t *px = malloc(N * N * 4);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
      float dx = (x + .5f) / N * 2 - 1, dy = (y + .5f) / N * 2 - 1, d = sqrtf(dx * dx + dy * dy);
      uint8_t *p = px + (y * N + x) * 4; p[0] = 8; p[1] = 6; p[2] = 14; p[3] = (uint8_t)(ck_clamp(1 - d, 0, 1) * 150);
    }
    t3_texture *T = t3_texture_new(N, N, px); free(px);
    M = t3_mesh_basic_material_new(0xffffff);
    t3_material_set_map(M, T); t3_release(T);
    M->transparent = true; M->depth_write = false; M->fog = false;
    G = t3_plane_geometry_new(1.1f, 1.1f, 1, 1);
  }
  t3_mesh *m = t3_mesh_new(G, M);
  t3_object_set_rotation(&m->base, -CK_PI / 2, 0, 0);
  m->base.render_order = 1; m->base.visible = false;
  return m;
}

static int kh_model(const char *letter) {
  int m = actor_model_find(letter);
  if (m < 0) { ck_log("body: no KEYHOLDER body %s", letter); m = AM_B; }
  return m;
}

body *body_new(int ri) {
  body *b = calloc(1, sizeof *b);
  b->ri = ri; b->e = roster_get(ri);
  const roster_entry *e = b->e;
  if (e->kind == BODY_KH) {
    actor_palette pal = ACTOR_PAL_NONE;
    actor_swatch *sw[4] = {&pal.hair, &pal.top, &pal.bottom, &pal.shoes};
    for (int i = 0; i < 4; i++) if (e->pal[i].sat > 0 || e->pal[i].val > 0) { sw[i]->hue = e->pal[i].hue; sw[i]->sat = e->pal[i].sat; sw[i]->val = e->pal[i].val; }
    b->actor = actor_new((actor_model)kh_model(e->body), &pal, e->scale, NULL);
    b->root = actor_root(b->actor);
    b->mixer = actor_mixer(b->actor);
    static const char *BN[BONE_N] = {"hips", "rightHand", "leftHand", "rightFoot", "rightToes"};
    for (int i = 0; i < BONE_N; i++) b->bone[i] = actor_bone(b->actor, BN[i]);
    /* KEYHOLDER's hold_place: +y out of the thumb side of the fist, +z along the knuckles */
    for (int h = 0; h < 2; h++) {
      t3_object *g = t3_object_new();
      t3_object_set_position(g, h ? .075f : -.075f, -.012f, .012f);
      t3_object_set_rotation(g, CK_PI / 2, 0, 0);
      t3_object_add(h ? actor_socket_lhand(b->actor) : actor_socket_rhand(b->actor), g);
      t3_release(g);
      b->grip[h] = g;
    }
    return b;
  }
  /* the page's Y8 */
  size_t n; uint8_t *data = ck_asset(e->glb, &n);
  b->gltf = data ? t3_gltf_parse(data, n, NULL, NULL) : NULL;
  free(data);
  b->root = t3_object_new();
  if (!b->gltf) { ck_log("body: cannot load %s", e->glb); return b; }
  t3_object *model = b->gltf->scene;
  t3_object_add(b->root, model);
  t3_object_set_scale(model, e->scale, e->scale, e->scale);
  mixer_init(&b->own, page_rig(e->rig), model);
  b->mixer = &b->own;
  t3_object_traverse(model, mesh_setup, NULL);
  static const char *BN[BONE_N] = {"Hips", "RightHand", "LeftHand", "RightFoot", "RightToeBase"};
  for (int i = 0; i < BONE_N; i++) b->bone[i] = t3_object_get_by_name(model, BN[i]);
  /* kl: the grip axis of the rig's hand; the left hand mirrors it */
  for (int h = 0; h < 2; h++) {
    t3_vec3 D = t3_v3(h ? -e->grip[0] : e->grip[0], e->grip[1], e->grip[2]);
    float l = sqrtf(D.x * D.x + D.y * D.y + D.z * D.z); D.x /= l; D.y /= l; D.z /= l;
    t3_object *g = t3_object_new();
    t3_object_set_quaternion(g, from_unit_vectors(t3_v3(0, 1, 0), D));
    t3_object_set_position(g, 0, 0.045f, 0);
    t3_object *hand = b->bone[h ? BONE_LHAND : BONE_RHAND];
    if (hand) t3_object_add(hand, g);
    t3_release(g);
    b->grip[h] = g;
  }
  b->blob = blob_new();
  return b;
}

void body_free(body *b) {
  if (!b) return;
  body_attach(b, NULL);
  if (b->actor) { actor_free(b->actor); free(b); return; }
  if (b->blob) t3_release(b->blob);
  t3_release(b->root);
  if (b->gltf) t3_release(b->gltf);
  free(b);
}

void body_attach(body *b, t3_scene *scene) {
  if (b->actor) { actor_attach(b->actor, scene); return; }
  t3_object *o[2] = {b->root, b->blob ? &b->blob->base : NULL};
  for (int i = 0; i < 2; i++) {
    if (!o[i]) continue;
    t3_retain(o[i]);
    if (o[i]->parent) t3_object_remove(o[i]->parent, o[i]);
    if (scene) t3_object_add(&scene->base, o[i]);
    t3_release(o[i]);
  }
  b->scene = scene;
}

int body_roster(const body *b) { return b->ri; }
t3_object *body_root(body *b) { return b->root; }
ck_mixer *body_mixer(body *b) { return b->mixer; }
const ck_rig *body_rig(const body *b) { return b->mixer ? b->mixer->rig : NULL; }
t3_object *body_bone(body *b, int w) { return w >= 0 && w < BONE_N ? b->bone[w] : NULL; }
t3_object *body_grip(body *b, int hand) { return b->grip[hand ? 1 : 0]; }

void body_animate(body *b, float dt) {
  if (b->actor) {
    t3_object *r = b->root;
    actor_move(b->actor, (v3){r->position.x, r->position.y, r->position.z}, r->rotation.y);
    actor_update(b->actor, dt);
    return;
  }
  if (b->mixer) mixer_update(b->mixer, dt);
  if (b->blob && b->blob->base.visible) t3_object_set_position(&b->blob->base, b->root->position.x, 0.02f, b->root->position.z);
  t3_object_update_matrix_world(b->root, true);
}

void body_teleport(body *b) {
  if (!b->actor) return;
  t3_object *r = b->root;
  actor_place(b->actor, (v3){r->position.x, r->position.y, r->position.z}, r->rotation.y);
}

void body_blob_shadow(body *b, bool on) {
  if (b->actor) { actor_shadow(b->actor, true); return; }   /* KEYHOLDER's bodies always have theirs */
  if (b->blob) b->blob->base.visible = on;
}

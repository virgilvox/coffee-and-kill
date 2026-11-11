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
  int ri, colour;
  const roster_entry *e;
  t3_object *root;                /* the page body: a group holding the scaled model; KEYHOLDER: the actor's root */
  t3_object *bone[BONE_N], *grip[2];
  ck_mixer *mixer;
  /* the page body */
  t3_gltf *gltf; ck_mixer own; t3_mesh *blob; t3_scene *scene; bool blob_on;
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

/* a KEYHOLDER body's palette in colour c (roster.h alts) */
static actor_palette kh_palette(const roster_entry *e, int c) {
  actor_palette pal = ACTOR_PAL_NONE;
  if (c < 1 || c >= ROSTER_COLOURS) return pal;
  actor_swatch *sw[4] = {&pal.hair, &pal.top, &pal.bottom, &pal.shoes};
  for (int i = 0; i < 4; i++) {
    const roster_swatch *r = &e->alts[c - 1][i];
    if (r->hue || r->sat || r->val || r->tint) { sw[i]->hue = r->hue; sw[i]->sat = r->sat ? r->sat : 1; sw[i]->val = r->val ? r->val : 1; sw[i]->tint = r->tint; }
  }
  return pal;
}

/* a page body in colour c: its scanned texture tinted by tone (roster.h). Neutral pixels (low
 * saturation, so not skin) take the dark tint below mid grey and the light tint above it, with
 * soft edges so the scan's shading carries over. */
static float smooth(float a, float b, float x) { float t = ck_clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static void hsv_rgb(float h, float s, float v, float *o) {
  h = fmodf(h / 60.f + 12, 6);
  float c = v * s, x = c * (1 - fabsf(fmodf(h, 2) - 1)), m = v - c;
  int k = (int)h;
  o[0] = (k == 0 || k == 5 ? c : k == 1 || k == 4 ? x : 0) + m;
  o[1] = (k == 1 || k == 2 ? c : k == 0 || k == 3 ? x : 0) + m;
  o[2] = (k == 3 || k == 4 ? c : k == 2 || k == 5 ? x : 0) + m;
}
static void page_tint(t3_texture *T, const roster_swatch *dark, const roster_swatch *light) {
  if (!T || !T->pixels || T->type != T3_UNSIGNED_BYTE_TYPE) return;
  bool hd = dark->hue || dark->sat || dark->val, hl = light->hue || light->sat || light->val;
  if (!hd && !hl) return;
  size_t n = (size_t)T->width * T->height;
  for (size_t i = 0; i < n; i++) {
    uint8_t *p = T->pixels + i * 4;
    float r = p[0] / 255.f, g = p[1] / 255.f, b = p[2] / 255.f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), v = mx, s = mx > 0 ? (mx - mn) / mx : 0;
    float wn = 1 - smooth(0.12f, 0.32f, s);
    if (wn <= 0) continue;
    float wd = hd ? wn * (1 - smooth(0.30f, 0.50f, v)) : 0, wl = hl ? wn * smooth(0.50f, 0.70f, v) : 0;
    const roster_swatch *t = wd >= wl ? dark : light; float w = fmaxf(wd, wl);
    if (w <= 0) continue;
    float o[3]; hsv_rgb(t->hue, t->sat, fminf(1, v * (t->val ? t->val : 1)), o);
    p[0] = (uint8_t)(ck_lerp(r, o[0], w) * 255 + .5f); p[1] = (uint8_t)(ck_lerp(g, o[1], w) * 255 + .5f); p[2] = (uint8_t)(ck_lerp(b, o[2], w) * 255 + .5f);
  }
  T->version++;
}
static void tint_meshes(t3_object *o, void *ctx) {
  const roster_swatch *sw = ctx;
  if (o->type != T3_MESH && o->type != T3_SKINNED_MESH) return;
  t3_mesh *m = (t3_mesh *)o;
  for (int i = 0; i < m->material_count; i++) if (m->materials[i] && m->materials[i]->map) page_tint(m->materials[i]->map, &sw[0], &sw[1]);
}

static int kh_model(const char *letter) {
  int m = actor_model_find(letter);
  if (m < 0) { ck_log("body: no KEYHOLDER body %s", letter); m = AM_B; }
  return m;
}

body *body_new(int ri, int colour) {
  body *b = calloc(1, sizeof *b);
  b->ri = ri; b->e = roster_get(ri);
  b->colour = colour >= 0 && colour < ROSTER_COLOURS ? colour : 0;
  const roster_entry *e = b->e;
  if (e->kind == BODY_KH) {
    actor_palette pal = kh_palette(e, b->colour);
    b->actor = actor_new((actor_model)kh_model(e->body), &pal, e->scale, NULL);
    b->root = actor_root(b->actor);
    b->mixer = actor_mixer(b->actor);
    static const char *BN[BONE_N] = {"hips", "rightHand", "leftHand", "rightFoot", "rightToes", "head"};
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
  if (b->colour) t3_object_traverse(model, tint_meshes, (void *)e->alts[b->colour - 1]);
  static const char *BN[BONE_N] = {"Hips", "RightHand", "LeftHand", "RightFoot", "RightToeBase", "Head"};
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
int body_colour(const body *b) { return b->colour; }
bool body_set_colour(body *b, int c) {
  if (c < 0 || c >= ROSTER_COLOURS) c = 0;
  if (c == b->colour) return true;
  if (!b->actor) return false;   /* a page body is tinted as it loads */
  actor_palette pal = kh_palette(b->e, c);
  actor_repalette(b->actor, &pal);
  b->colour = c;
  return true;
}
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
  b->blob_on = on;
  if (b->blob) b->blob->base.visible = on && b->root->visible;
}

void body_visible(body *b, bool on) {
  if (b->actor) { actor_visible(b->actor, on); return; }
  b->root->visible = on;
  if (b->blob && !on) b->blob->base.visible = false;
  if (b->blob && on) b->blob->base.visible = b->scene && b->blob_on;
}

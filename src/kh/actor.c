/* actor.c: see actor.h. Taken from KEYHOLDER (src/actor.c) for its bodies; the changes here: the
 * bodies' assets are kh_<name> and their clips rig_vroid.bin, an actor can be freed and its model
 * unloaded once nothing uses it, moved to another scene, and its mixer driven by the fighter.
 * The spring bones follow the VRM 0 secondary animation
 * rule: each joint's tail is a verlet point pulled back toward its rest direction
 * by stiffness, pushed by gravity, kept at its length, pushed out of collider
 * spheres, and the joint turned to point at it. */
#include "actor.h"
#include "anim.h"
#include "gen/cast_meta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const actor_palette ACTOR_PAL_NONE = {{0, 1, 1}, {0, 1, 1}, {0, 1, 1}, {0, 1, 1}};

#define MAX_JOINTS 96
#define MAX_COLLIDERS 24
typedef struct {
  t3_object *bone, *parent;
  float axis[3], len;
  float tail[3], prev[3];
  float stiff, grav, drag, radius;
} sp_joint;
typedef struct { t3_object *bone; float r, off[3]; } sp_collider;

struct actor {
  actor_model model;
  float scale, floor_y;
  t3_object *root, *gltf_scene;
  t3_lod *lod;
  t3_mesh *face, *body, *outline, *shadow;
  t3_material *m_body, *m_face;
  t3_texture *tex;                    /* the body atlas in this actor's palette (shared through PALC) */
  ck_mixer mixer;
  ck_action *cur;
  int cur_clip;
  v3 pos; float yaw;
  t3_object *b_neck, *b_head, *b_chest, *b_rhand, *b_lhand;
  t3_object *s_rhand, *s_lhand;
  /* look */
  v3 look_at; float look_tgt, look_w, look_yaw, look_pitch;
  /* face */
  float ex[EX_COUNT], ex_t[EX_COUNT];
  float blink_t, blink_v;
  float mouth, mouth_shape;
  /* springs */
  sp_joint j[MAX_JOINTS]; int nj;
  sp_collider c[MAX_COLLIDERS]; int nc;
  bool spring_reset;
  /* hands up */
  t3_object *b_arm[4], *b_trunk[7];
  v3 reach[2]; float reach_t, reach_w;   /* hand targets (right, left) in world space: actor_reach */
  float hands_t, hands_w;
};

static ck_rig *RIG;
/* each model parsed once (actor_preload); actors are clones that share its geometry and face atlas */
typedef struct {
  t3_gltf *gltf;
  int users;                          /* actors made from it: at none, actor_free unloads it */
  uint8_t *pristine; int tw, th;      /* the body atlas before any palette */
  t3_texture *face_tex;
  char *spr;                          /* the spring bone text */
} model_tpl;
static model_tpl TPL[AM_COUNT];
/* body atlases by palette: actors in the same clothes share one texture (and one upload) */
#define PALC_N 48
static struct { actor_model m; actor_palette p; t3_texture *T; } PALC[PALC_N];
static float LOD_BIAS = 1;
static const t3_camera *LOD_CAM;
#define LOD_NEAR 5.f     /* full body, face morphs and the outline hull inside this */
#define LOD_FAR 10.f     /* the far body with a hull inside this, without beyond */
#define LOD_FOV 50.f     /* the distances hold at this field of view; a longer lens keeps detail further */

/* ---- small quaternion helpers on {x, y, z, w} ---- */
static void qmul(float *o, const float *a, const float *b) {
  float x = a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1];
  float y = a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0];
  float z = a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3];
  float w = a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2];
  o[0] = x; o[1] = y; o[2] = z; o[3] = w;
}
static void qrot(float *o, const float *q, const float *v) {
  float t[3] = { 2*(q[1]*v[2] - q[2]*v[1]), 2*(q[2]*v[0] - q[0]*v[2]), 2*(q[0]*v[1] - q[1]*v[0]) };
  float x = v[0] + q[3]*t[0] + q[1]*t[2] - q[2]*t[1];
  float y = v[1] + q[3]*t[1] + q[2]*t[0] - q[0]*t[2];
  float z = v[2] + q[3]*t[2] + q[0]*t[1] - q[1]*t[0];
  o[0] = x; o[1] = y; o[2] = z;
}
static void qinv(float *o, const float *q) { o[0] = -q[0]; o[1] = -q[1]; o[2] = -q[2]; o[3] = q[3]; }
static void qaxis(float *o, float x, float y, float z, float angle) {
  float s = sinf(angle * .5f); o[0] = x * s; o[1] = y * s; o[2] = z * s; o[3] = cosf(angle * .5f);
}
/* the rotation of an object's world matrix (scale taken out) */
static void wquat(const t3_object *ob, float *q) {
  const float *m = ob->matrix_world.e;
  float sx = sqrtf(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]), sy = sqrtf(m[4]*m[4] + m[5]*m[5] + m[6]*m[6]), sz = sqrtf(m[8]*m[8] + m[9]*m[9] + m[10]*m[10]);
  float a = m[0]/sx, b = m[4]/sy, c = m[8]/sz, d = m[1]/sx, e = m[5]/sy, f = m[9]/sz, g = m[2]/sx, h = m[6]/sy, i = m[10]/sz;
  float t = a + e + i, s;
  if (t > 0) { s = .5f / sqrtf(t + 1); q[3] = .25f / s; q[0] = (h - f) * s; q[1] = (c - g) * s; q[2] = (d - b) * s; }
  else if (a > e && a > i) { s = 2 * sqrtf(1 + a - e - i); q[3] = (h - f) / s; q[0] = .25f * s; q[1] = (b + d) / s; q[2] = (c + g) / s; }
  else if (e > i) { s = 2 * sqrtf(1 + e - a - i); q[3] = (c - g) / s; q[0] = (b + d) / s; q[1] = .25f * s; q[2] = (f + h) / s; }
  else { s = 2 * sqrtf(1 + i - a - e); q[3] = (d - b) / s; q[0] = (c + g) / s; q[1] = (f + h) / s; q[2] = .25f * s; }
}
static void wpos(const t3_object *ob, float *p) { const float *m = ob->matrix_world.e; p[0] = m[12]; p[1] = m[13]; p[2] = m[14]; }

/* ---- palette swap on the body atlas ---- */
static void hsv_turn(uint8_t *px, const actor_swatch *s) {
  float r = px[0] / 255.f, g = px[1] / 255.f, b = px[2] / 255.f;
  float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn, h = 0;
  if (d > 1e-4f) { if (mx == r) h = fmodf((g - b) / d + 6, 6); else if (mx == g) h = (b - r) / d + 2; else h = (r - g) / d + 4; }
  float sat = mx > 0 ? d / mx : 0, v = mx;
  h = fmodf(h + s->hue / 60.f + 12, 6); sat = ds_clamp(sat * s->sat, 0, 1); v = ds_clamp(v * s->val, 0, 1);
  float c = v * sat, x = c * (1 - fabsf(fmodf(h, 2) - 1)), m = v - c, o[3];
  int k = (int)h;
  o[0] = k == 0 || k == 5 ? c : k == 1 || k == 4 ? x : 0;
  o[1] = k == 1 || k == 2 ? c : k == 0 || k == 3 ? x : 0;
  o[2] = k == 3 || k == 4 ? c : k == 2 || k == 5 ? x : 0;
  for (int i = 0; i < 3; i++) px[i] = (uint8_t)((o[i] + m) * 255.f + .5f);
}
static void apply_palette(t3_texture *T, actor_model model, const actor_palette *pal) {
  if (!T || !T->pixels || !pal) return;
  if ((int)model >= (int)(sizeof CAST_META / sizeof CAST_META[0])) return;
  const cast_meta *M = &CAST_META[model];
  const actor_swatch *sw[4] = { &pal->hair, &pal->top, &pal->bottom, &pal->shoes };
  for (int i = 0; i < M->nrect; i++) {
    const cast_rect *r = &M->rect[i];
    const actor_swatch *s = sw[r->kind];
    if (s->hue == 0 && s->sat == 1 && s->val == 1) continue;
    for (int y = r->y; y < r->y + r->h && y < T->height; y++)
      for (int x = r->x; x < r->x + r->w && x < T->width; x++) hsv_turn(T->pixels + (size_t)(y * T->width + x) * 4, s);
  }
  T->version++;
}

/* ---- models: parsed once, cloned per actor ---- */
static t3_texture *find_map(t3_object *root, const char *mesh) {
  t3_object *o = t3_object_get_by_name(root, mesh);
  if (!o || (o->type != T3_MESH && o->type != T3_SKINNED_MESH)) return NULL;
  t3_mesh *m = (t3_mesh *)o;
  return m->material_count && m->materials[0] ? m->materials[0]->map : NULL;
}
static void atlas_setup(t3_texture *T) {
  T->color_space = T3_SRGB_COLOR_SPACE; T->flip_y = false; T->wrap_s = T->wrap_t = T3_CLAMP_TO_EDGE;
  T->generate_mipmaps = true; T->min_filter = T3_LINEAR_MIPMAP_LINEAR; T->mag_filter = T3_LINEAR;
  T->version++;
}
bool actor_preload(actor_model model) {
  if ((unsigned)model >= AM_COUNT) return false;
  model_tpl *P = &TPL[model];
  if (P->gltf) return true;
  if (!RIG) RIG = rig_load("rig_vroid.bin");
  char name[48]; snprintf(name, sizeof name, "kh_%s", CAST_META[model].glb);
  size_t n; uint8_t *b = ds_asset(name, &n);
  if (b) { P->gltf = t3_gltf_parse(b, n, NULL, NULL); free(b); }
  if (!P->gltf) { ds_log("actor: cannot parse %s", CAST_META[model].glb); return false; }
  t3_texture *B = find_map(P->gltf->scene, "body");
  if (B && B->pixels) {
    P->tw = B->width; P->th = B->height;
    P->pristine = malloc((size_t)B->width * B->height * 4);
    memcpy(P->pristine, B->pixels, (size_t)B->width * B->height * 4);
    /* the template never draws: let its copy of the body atlas go (actors get theirs from body_tex) */
    static const char *USE[2] = {"body", "far"};
    for (int i = 0; i < 2; i++) { t3_object *o = t3_object_get_by_name(P->gltf->scene, USE[i]); if (o && ((t3_mesh *)o)->materials[0]) t3_material_set_map(((t3_mesh *)o)->materials[0], NULL); }
  }
  P->face_tex = find_map(P->gltf->scene, "face");
  if (P->face_tex) { t3_retain(P->face_tex); atlas_setup(P->face_tex); }
  snprintf(name, sizeof name, "kh_%s", CAST_META[model].spr);
  P->spr = (char *)ds_asset(name, &n);
  return true;
}
static void unload(actor_model model) {
  model_tpl *P = &TPL[model];
  for (int i = 0; i < PALC_N; i++) if (PALC[i].T && PALC[i].m == model) { t3_release(PALC[i].T); PALC[i].T = NULL; }
  if (P->face_tex) t3_release(P->face_tex);
  if (P->gltf) t3_release(P->gltf);
  free(P->pristine); free(P->spr);
  memset(P, 0, sizeof *P);
}
void actor_preload_all(void) { for (int m = 0; m < AM_COUNT; m++) actor_preload((actor_model)m); }

/* the body atlas in a palette: shared by every actor of this model in the same palette */
static t3_texture *body_tex(actor_model model, const actor_palette *pal) {
  model_tpl *P = &TPL[model];
  if (!P->pristine) return NULL;
  if (!pal) pal = &ACTOR_PAL_NONE;
  int slot = -1;
  for (int i = 0; i < PALC_N; i++) {
    if (PALC[i].T && PALC[i].m == model && !memcmp(&PALC[i].p, pal, sizeof *pal)) { t3_retain(PALC[i].T); return PALC[i].T; }
    if (slot < 0 && (!PALC[i].T || PALC[i].T->_refs <= 1)) slot = i;   /* empty, or no actor uses it any more */
  }
  t3_texture *T = t3_texture_new(P->tw, P->th, P->pristine);
  apply_palette(T, model, pal);
  atlas_setup(T);
  if (slot >= 0) {
    if (PALC[slot].T) t3_release(PALC[slot].T);
    PALC[slot].m = model; PALC[slot].p = *pal; PALC[slot].T = T; t3_retain(T);
  }
  return T;   /* the caller's reference */
}

/* a copy of the template's node tree: bones are new objects, meshes share geometry */
#define MAX_NODES 320
typedef struct { t3_object *src[MAX_NODES], *dst[MAX_NODES]; int n; } node_map;
static t3_object *clone_node(t3_object *src, node_map *M) {
  t3_object *o;
  if (src->type == T3_SKINNED_MESH) {
    t3_mesh *sm = (t3_mesh *)src;
    t3_skinned_mesh *k = t3_skinned_mesh_new(sm->geometry, sm->materials[0]);
    o = &k->mesh.base;
    if (sm->morph_influence_count) t3_mesh_update_morph_targets(&k->mesh);
  } else if (src->type == T3_BONE) o = t3_bone_new();
  else o = t3_group_new();
  o->name = src->name;   /* the template's strings: it lives for the game */
  o->position = src->position; o->quaternion = src->quaternion; o->rotation = src->rotation; o->scale = src->scale;
  if (M->n < MAX_NODES) { M->src[M->n] = src; M->dst[M->n] = o; M->n++; }
  for (int i = 0; i < src->child_count; i++) { t3_object *c = clone_node(src->children[i], M); t3_object_add(o, c); t3_release(c); }
  return o;
}
static t3_object *mapped(node_map *M, t3_object *src) { for (int i = 0; i < M->n; i++) if (M->src[i] == src) return M->dst[i]; return NULL; }
static void bind_clones(node_map *M) {
  for (int i = 0; i < M->n; i++) {
    if (M->src[i]->type != T3_SKINNED_MESH) continue;
    t3_skinned_mesh *s = (t3_skinned_mesh *)M->src[i], *d = (t3_skinned_mesh *)M->dst[i];
    if (!s->skeleton) continue;
    int n = s->skeleton->bone_count;
    t3_object **bones = malloc(sizeof *bones * (size_t)(n ? n : 1));
    for (int j = 0; j < n; j++) bones[j] = mapped(M, s->skeleton->bones[j]);
    t3_skeleton *sk = t3_skeleton_new(bones, n, s->skeleton->bone_inverses);
    t3_skinned_mesh_bind(d, sk, &s->bind_matrix);
    t3_release(sk); free(bones);
  }
}

/* materials: cel lambert off the atlases (one each for body and face, so a flash is one colour change), dark flat hull */
static t3_material *outline_mat(void) {
  static t3_material *M;
  if (!M) { M = t3_mesh_basic_material_new(0x14101a); M->fog = true; }
  return M;
}
static t3_material *cel_mat(t3_texture *T) {
  t3_material *M = t3_mesh_lambert_material_new(0xffffff);
  if (T) t3_material_set_map(M, T);
  M->alpha_test = 0.45f; M->side = T3_DOUBLE_SIDE; M->fog = true;
  return M;
}
static void set_mat(t3_object *o, t3_material *M) {
  if (!o || (o->type != T3_MESH && o->type != T3_SKINNED_MESH)) return;
  t3_mesh *m = (t3_mesh *)o;
  o->frustum_culled = false; o->cast_shadow = false; o->receive_shadow = false;
  t3_retain(M);
  if (m->materials[0]) t3_release(m->materials[0]);
  m->materials[0] = M;
}
/* a second instance of a mesh (same geometry, same skeleton) for another LOD level */
static t3_object *twin(t3_object *src) {
  if (!src) return NULL;
  t3_skinned_mesh *s = (t3_skinned_mesh *)src;
  t3_skinned_mesh *k = t3_skinned_mesh_new(s->mesh.geometry, s->mesh.materials[0]);
  k->mesh.base.name = src->name; k->mesh.base.frustum_culled = false;
  t3_skinned_mesh_bind(k, s->skeleton, &s->bind_matrix);
  return &k->mesh.base;
}

/* ---- springs ---- */
static void add_chain(actor_t *a, t3_object *bone, t3_object *parent, float stiff, float grav, float drag, float radius) {
  if (a->nj >= MAX_JOINTS) return;
  sp_joint *j = &a->j[a->nj++];
  j->bone = bone; j->parent = parent; j->stiff = stiff; j->grav = grav; j->drag = drag; j->radius = radius;
  /* the tail: the first child, or 7 cm on along the bone */
  float ax[3];
  if (bone->child_count) { t3_object *c = bone->children[0]; ax[0] = c->position.x; ax[1] = c->position.y; ax[2] = c->position.z; }
  else { ax[0] = bone->position.x; ax[1] = bone->position.y; ax[2] = bone->position.z; float l = sqrtf(ax[0]*ax[0]+ax[1]*ax[1]+ax[2]*ax[2]); if (l > 1e-5f) { ax[0] *= .07f/l; ax[1] *= .07f/l; ax[2] *= .07f/l; } }
  float l = sqrtf(ax[0]*ax[0] + ax[1]*ax[1] + ax[2]*ax[2]);
  j->len = l;
  if (l > 1e-5f) { j->axis[0] = ax[0]/l; j->axis[1] = ax[1]/l; j->axis[2] = ax[2]/l; } else { j->axis[1] = -1; j->len = .05f; }
  for (int i = 0; i < bone->child_count; i++) add_chain(a, bone->children[i], bone, stiff, grav, drag, radius);
}
static void load_springs(actor_t *a) {
  const char *src = TPL[a->model].spr;
  if (!src) return;
  char *t = strdup(src);
  if (!t) return;
  char *save = NULL;
  for (char *line = strtok_r(t, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    char name[32]; float r, x, y, z, st, gr, dr, hr; int cnt, off = 0;
    if (line[0] == 'c' && sscanf(line, "c %31s %f %f %f %f", name, &r, &x, &y, &z) == 5) {
      if (a->nc >= MAX_COLLIDERS) continue;
      t3_object *b = t3_object_get_by_name(a->gltf_scene, name);
      if (b) { sp_collider *c = &a->c[a->nc++]; c->bone = b; c->r = r; c->off[0] = x; c->off[1] = y; c->off[2] = z; }
    } else if (line[0] == 'g' && sscanf(line, "g %f %f %f %f %d%n", &st, &gr, &dr, &hr, &cnt, &off) == 5) {
      char *p = line + off;
      for (int i = 0; i < cnt; i++) {
        int adv = 0;
        if (sscanf(p, " %31s%n", name, &adv) != 1) break;
        p += adv;
        t3_object *b = t3_object_get_by_name(a->gltf_scene, name);
        if (b && b->parent) add_chain(a, b, b->parent, st, gr, dr, hr);
      }
    }
  }
  free(t);
}
static void spring_seed(actor_t *a) {
  for (int i = 0; i < a->nj; i++) {
    sp_joint *j = &a->j[i];
    float q[4], p[3], d[3];
    wquat(j->bone, q); wpos(j->bone, p);
    float s = a->scale;
    qrot(d, q, j->axis);
    for (int k = 0; k < 3; k++) { j->tail[k] = p[k] + d[k] * j->len * s; j->prev[k] = j->tail[k]; }
  }
}
static void spring_step(actor_t *a, float dt) {
  float s = a->scale;
  float cp[MAX_COLLIDERS][3];
  for (int c = 0; c < a->nc; c++) {
    float q[4], p[3], o[3]; sp_collider *C = &a->c[c];
    wquat(C->bone, q); wpos(C->bone, p);
    qrot(o, q, C->off);
    for (int k = 0; k < 3; k++) cp[c][k] = p[k] + o[k] * s;
  }
  for (int i = 0; i < a->nj; i++) {
    sp_joint *j = &a->j[i];
    t3_object_update_world_matrix(j->parent, false, false);
    float pq[4], p[3], d[3];
    wquat(j->parent, pq);
    /* the bone's own head: the parent's frame plus its local offset */
    float lo[3] = { j->bone->position.x, j->bone->position.y, j->bone->position.z }, wo[3], pp[3];
    qrot(wo, pq, lo); wpos(j->parent, pp);
    for (int k = 0; k < 3; k++) p[k] = pp[k] + wo[k] * s;
    qrot(d, pq, j->axis);
    float next[3], len = j->len * s, keep = 1 - j->drag;
    float scale_dt = dt * 60.f;   /* keep the source's feel at any frame rate */
    for (int k = 0; k < 3; k++) next[k] = j->tail[k] + (j->tail[k] - j->prev[k]) * keep + d[k] * j->stiff * dt * len * 6.f;
    next[1] -= j->grav * dt * .9f;
    (void)scale_dt;
    /* keep the length */
    float v[3] = { next[0]-p[0], next[1]-p[1], next[2]-p[2] };
    float l = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); if (l < 1e-5f) l = 1e-5f;
    for (int k = 0; k < 3; k++) next[k] = p[k] + v[k] / l * len;
    /* out of the colliders */
    for (int c = 0; c < a->nc; c++) {
      float dv[3] = { next[0]-cp[c][0], next[1]-cp[c][1], next[2]-cp[c][2] };
      float rr = (a->c[c].r + j->radius) * s, dl = sqrtf(dv[0]*dv[0] + dv[1]*dv[1] + dv[2]*dv[2]);
      if (dl < rr && dl > 1e-5f) {
        for (int k = 0; k < 3; k++) next[k] = cp[c][k] + dv[k] / dl * rr;
        float w[3] = { next[0]-p[0], next[1]-p[1], next[2]-p[2] }; float wl = sqrtf(w[0]*w[0] + w[1]*w[1] + w[2]*w[2]); if (wl < 1e-5f) wl = 1e-5f;
        for (int k = 0; k < 3; k++) next[k] = p[k] + w[k] / wl * len;
      }
    }
    memcpy(j->prev, j->tail, sizeof j->tail); memcpy(j->tail, next, sizeof next);
    /* turn the bone to the tail, in the parent's frame */
    float pi[4], dir[3], w[3] = { next[0]-p[0], next[1]-p[1], next[2]-p[2] };
    qinv(pi, pq); qrot(dir, pi, w);
    float dl = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]); if (dl < 1e-5f) dl = 1e-5f;
    for (int k = 0; k < 3; k++) dir[k] /= dl;
    float dt2 = j->axis[0]*dir[0] + j->axis[1]*dir[1] + j->axis[2]*dir[2];
    float q[4] = { j->axis[1]*dir[2] - j->axis[2]*dir[1], j->axis[2]*dir[0] - j->axis[0]*dir[2], j->axis[0]*dir[1] - j->axis[1]*dir[0], 1 + dt2 };
    float ql = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (ql < 1e-5f) { q[0] = 0; q[1] = 0; q[2] = 0; q[3] = 1; ql = 1; }
    t3_quat Q = { q[0]/ql, q[1]/ql, q[2]/ql, q[3]/ql };
    t3_object_set_quaternion(j->bone, Q);
    t3_object_update_world_matrix(j->bone, false, false);
  }
}

actor_t *actor_new(actor_model model, const actor_palette *pal, float scale, t3_scene *scene) {
  actor_t *a = calloc(1, sizeof *a);
  if ((unsigned)model >= AM_COUNT) model = AM_B;
  a->model = model; a->scale = scale > 0 ? scale : 1; a->cur_clip = -1; a->blink_t = ds_rr(1, 4);
  a->root = t3_object_new();
  if (!actor_preload(model)) { if (scene) t3_object_add(&scene->base, a->root); return a; }
  model_tpl *P = &TPL[model];
  P->users++;
  node_map *M = calloc(1, sizeof *M);
  a->gltf_scene = clone_node(P->gltf->scene, M);
  bind_clones(M);
  free(M);
  t3_object_add(a->root, a->gltf_scene);
  /* the levels: near (body, face, hull), mid (far body, its hull), far (far body alone); the renderer picks by camera distance */
  a->tex = body_tex(model, pal);
  a->m_body = cel_mat(a->tex); a->m_face = cel_mat(P->face_tex);
  static const char *MESH[6] = {"body", "face", "outline", "far", "farface", "faroutline"};
  t3_object *mo[6] = {0};
  for (int i = 0; i < 6; i++) {
    t3_object *o = t3_object_get_by_name(a->gltf_scene, MESH[i]);
    if (!o || o->type != T3_SKINNED_MESH) continue;
    t3_retain(o); t3_object_remove(o->parent, o);
    set_mat(o, i == 2 || i == 5 ? outline_mat() : i == 1 || i == 4 ? a->m_face : a->m_body);
    mo[i] = o;
  }
  a->body = (t3_mesh *)mo[0]; a->face = (t3_mesh *)mo[1]; a->outline = (t3_mesh *)mo[2];
  if (a->face) t3_mesh_update_morph_targets(a->face);
  a->lod = t3_lod_new();
  t3_object *lv[3] = { t3_group_new(), t3_group_new(), t3_group_new() };
  for (int i = 0; i < 3; i++) if (mo[i]) t3_object_add(lv[0], mo[i]);
  if (mo[3]) {   /* an old bake without the far meshes keeps the near level everywhere */
    for (int i = 3; i < 6; i++) if (mo[i]) t3_object_add(lv[1], mo[i]);
    t3_object *f = twin(mo[3]), *ff = twin(mo[4]);
    if (f) { t3_object_add(lv[2], f); t3_release(f); }
    if (ff) { t3_object_add(lv[2], ff); t3_release(ff); }
  }
  t3_lod_add_level(a->lod, lv[0], 0);
  if (mo[3]) { t3_lod_add_level(a->lod, lv[1], LOD_NEAR); t3_lod_add_level(a->lod, lv[2], LOD_FAR); }
  for (int i = 0; i < 3; i++) t3_release(lv[i]);
  for (int i = 0; i < 6; i++) if (mo[i]) t3_release(mo[i]);
  t3_object_add(a->root, a->lod);
  t3_release(a->lod);
  if (RIG) { mixer_init(&a->mixer, RIG, a->gltf_scene); a->mixer.hips_scale = CAST_META[model].hips_h; }
  a->b_neck = t3_object_get_by_name(a->gltf_scene, "neck");
  a->b_head = t3_object_get_by_name(a->gltf_scene, "head");
  a->b_chest = t3_object_get_by_name(a->gltf_scene, "upperChest");
  a->b_rhand = t3_object_get_by_name(a->gltf_scene, "rightHand");
  a->b_lhand = t3_object_get_by_name(a->gltf_scene, "leftHand");
  { static const char *ARMS[4] = {"rightUpperArm", "rightLowerArm", "leftUpperArm", "leftLowerArm"};
    for (int i = 0; i < 4; i++) a->b_arm[i] = t3_object_get_by_name(a->gltf_scene, ARMS[i]);
    static const char *TRUNK[7] = {"spine", "chest", "upperChest", "neck", "head", "rightShoulder", "leftShoulder"};
    for (int i = 0; i < 7; i++) a->b_trunk[i] = t3_object_get_by_name(a->gltf_scene, TRUNK[i]); }
  a->s_rhand = t3_object_new(); a->s_lhand = t3_object_new();
  if (a->b_rhand) t3_object_add(a->b_rhand, a->s_rhand);
  if (a->b_lhand) t3_object_add(a->b_lhand, a->s_lhand);
  load_springs(a);
  /* the blob shadow */
  static t3_texture *ST; static t3_material *SM; static t3_geometry *SG;
  if (!SM) {
    enum { N = 32 };
    uint8_t *px = malloc(N * N * 4);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
      float dx = (x + .5f) / N * 2 - 1, dy = (y + .5f) / N * 2 - 1, d = sqrtf(dx*dx + dy*dy);
      uint8_t *p = px + (y * N + x) * 4; p[0] = 8; p[1] = 6; p[2] = 14; p[3] = (uint8_t)(ds_clamp(1 - d, 0, 1) * 150);
    }
    ST = t3_texture_new(N, N, px); free(px);
    SM = t3_mesh_basic_material_new(0xffffff);
    t3_material_set_map(SM, ST); SM->transparent = true; SM->depth_write = false; SM->fog = false;
    SG = t3_plane_geometry_new(1.1f, 1.1f, 1, 1);
  }
  a->shadow = t3_mesh_new(SG, SM);
  t3_object_set_rotation(&a->shadow->base, -DS_PI / 2, 0, 0);
  t3_object_set_position(&a->shadow->base, 0, .02f, 0);
  a->shadow->base.render_order = 1;
  if (scene) { t3_object_add(&scene->base, &a->shadow->base); t3_object_add(&scene->base, a->root); }
  actor_play(a, "Idle_Loop", 1, true, 0);
  actor_place(a, v3_(0, 0, 0), 0);
  return a;
}

void actor_move(actor_t *a, v3 pos, float yaw) {
  a->pos = pos; a->yaw = yaw;
  t3_object_set_position(a->root, pos.x, pos.y, pos.z);
  t3_object_set_rotation(a->root, 0, yaw, 0);
  t3_object_set_scale(a->root, a->scale, a->scale, a->scale);
  if (a->shadow) t3_object_set_position(&a->shadow->base, pos.x, a->floor_y + .02f, pos.z);
}
void actor_place(actor_t *a, v3 pos, float yaw) { a->floor_y = pos.y; actor_move(a, pos, yaw); a->spring_reset = true; }
void actor_set_floor(actor_t *a, float f) { a->floor_y = f; }
v3 actor_pos(const actor_t *a) { return a->pos; }
float actor_yaw(const actor_t *a) { return a->yaw; }
void actor_visible(actor_t *a, bool on) { a->root->visible = on; if (a->shadow) a->shadow->base.visible = on; }
bool actor_is_visible(const actor_t *a) { return a->root->visible; }
float actor_height(const actor_t *a) { return CAST_META[a->model].height * a->scale; }
float actor_hips_height(const actor_t *a) { return CAST_META[a->model].hips_h * a->scale; }

static void play(actor_t *a, const char *clip, float speed, bool loop, float fade, bool restart) {
  if (!RIG) return;
  int c = rig_clip_index(RIG, clip);
  if (c < 0) { ds_log("actor: no clip %s", clip); return; }
  ck_action *ac = mixer_action(&a->mixer, c);
  if (c == a->cur_clip && a->cur && !restart) { ac->time_scale = speed; return; }
  if (c == a->cur_clip && a->cur) { action_reset(ac); ac->time_scale = speed; return; }
  action_reset(ac);
  ac->loop = loop ? LOOP_REPEAT : LOOP_ONCE;
  ac->clamp_when_finished = !loop;
  ac->time_scale = speed;
  action_play(&a->mixer, ac);
  if (a->cur && fade > 0) action_cross_fade_from(&a->mixer, ac, a->cur, fade);
  else {
    if (a->cur) action_stop(&a->mixer, a->cur);
    action_set_effective_weight(ac, 1);
  }
  a->cur = ac; a->cur_clip = c;
}
void actor_play(actor_t *a, const char *clip, float speed, bool loop, float fade) { play(a, clip, speed, loop, fade, false); }
void actor_replay(actor_t *a, const char *clip, float speed, bool loop, float fade) { play(a, clip, speed, loop, fade, true); }
bool actor_clip_done(const actor_t *a) { return a->cur && a->cur->loop == LOOP_ONCE && a->cur->paused; }
float actor_clip_time(const actor_t *a) { return a->cur ? a->cur->time : 0; }
float actor_clip_len(const actor_t *a) { return a->cur ? action_duration(&a->mixer, a->cur) : 0; }
void actor_clip_seek(actor_t *a, float t) { if (a->cur) { a->cur->time = t; a->cur->paused = false; a->cur->enabled = true; } }
float actor_clip_ground_speed(const actor_t *a) { return a->cur_clip >= 0 && RIG ? RIG->clips[a->cur_clip].speed * actor_hips_height(a) : 0; }

void actor_look(actor_t *a, v3 t, float amount) { a->look_at = t; a->look_tgt = amount; }
void actor_expr_set(actor_t *a, actor_expr e, float v) { a->ex_t[e] = v; }
void actor_expr_clear(actor_t *a) { for (int i = EX_JOY; i <= EX_FUN; i++) a->ex_t[i] = 0; }
void actor_mouth(actor_t *a, float open, float shape) { a->mouth = open; a->mouth_shape = shape; }

static void apply_look(actor_t *a, float dt) {
  a->look_w = ds_damp(a->look_w, a->look_tgt, 6, dt);
  if (a->look_w < 0.005f || !a->b_head) return;
  v3 h = actor_head_pos(a);
  float dx = a->look_at.x - h.x, dy = a->look_at.y - h.y, dz = a->look_at.z - h.z;
  float want = ds_wrap_pi(atan2f(dx, dz) - a->yaw);
  float pitch = atan2f(dy, sqrtf(dx*dx + dz*dz));
  want = ds_clamp(want, -1.15f, 1.15f); pitch = ds_clamp(pitch, -.5f, .45f);
  a->look_yaw = ds_damp(a->look_yaw, want, 8, dt); a->look_pitch = ds_damp(a->look_pitch, pitch, 8, dt);
  float qy[4], qp[4], d[4], cur[4], o[4];
  qaxis(qy, 0, 1, 0, a->look_yaw * a->look_w * .5f);
  qaxis(qp, 1, 0, 0, -a->look_pitch * a->look_w * .5f);
  qmul(d, qy, qp);
  t3_object *bones[2] = { a->b_neck, a->b_head };
  for (int i = 0; i < 2; i++) {
    t3_object *b = bones[i]; if (!b) continue;
    cur[0] = b->quaternion.x; cur[1] = b->quaternion.y; cur[2] = b->quaternion.z; cur[3] = b->quaternion.w;
    qmul(o, d, cur);
    t3_quat Q = { o[0], o[1], o[2], o[3] };
    t3_object_set_quaternion(b, Q);
  }
}

void actor_repalette(actor_t *a, const actor_palette *pal) {
  if (!a->m_body) return;
  t3_texture *T = body_tex(a->model, pal);
  if (!T) return;
  t3_material_set_map(a->m_body, T);
  if (a->tex) t3_release(a->tex);
  a->tex = T;
}
void actor_lod_bias(float bias) { LOD_BIAS = bias > 0 ? bias : 1; }
void actor_lod_camera(const t3_camera *cam) { LOD_CAM = cam; }
static float lod_scale(void) {
  float k = LOD_BIAS;
  if (LOD_CAM && LOD_CAM->fov > 1) k *= tanf(LOD_FOV * DS_PI / 360.f) / tanf(LOD_CAM->fov * DS_PI / 360.f);
  return k;
}
int actor_lod_level(const actor_t *a) { return a->lod ? a->lod->current_level : 0; }
void actor_hands_up(actor_t *a, float amount) { a->hands_t = amount; }
static void apply_hands(actor_t *a, float dt) {
  a->hands_w = ds_damp(a->hands_w, a->hands_t, 5, dt);
  if (a->hands_w < 0.005f) return;
  /* the back straightens over whatever the clip was doing */
  for (int i = 0; i < 7; i++) if (a->b_trunk[i]) t3_object_set_quaternion(a->b_trunk[i], t3_quat_slerp(a->b_trunk[i]->quaternion, t3_quat_identity(), a->hands_w * .9f));
  /* in the bone frames, T pose at identity: upper arms out and a little down, forearms straight up */
  static const float ang[4][3] = { {.35f, .12f, 0}, {-1.5f, 0, 0}, {-.35f, -.12f, 0}, {1.5f, 0, 0} };
  for (int i = 0; i < 4; i++) {
    t3_object *b = a->b_arm[i]; if (!b) continue;
    t3_quat tgt = t3_quat_multiply(t3_quat_from_axis_angle(t3_v3(0, 1, 0), ang[i][1]), t3_quat_from_axis_angle(t3_v3(0, 0, 1), ang[i][0]));
    t3_object_set_quaternion(b, t3_quat_slerp(b->quaternion, tgt, a->hands_w));
  }
}


/* ---- reach: the hands onto two world points over whatever the clip plays (a bike's grips) ---- */
void actor_reach(actor_t *a, v3 right, v3 left, float amount) { a->reach[0] = right; a->reach[1] = left; a->reach_t = amount; }
static void qconj(float *o, const float *q) { o[0] = -q[0]; o[1] = -q[1]; o[2] = -q[2]; o[3] = q[3]; }
static void qfrom_to(float *o, v3 u, v3 v) {   /* the shortest turn taking unit u onto unit v */
  float d = v3_dot(u, v);
  if (d < -.9999f) { qaxis(o, 0, 1, 0, DS_PI); return; }
  v3 c = v3_cross(u, v);
  o[0] = c.x; o[1] = c.y; o[2] = c.z; o[3] = 1 + d;
  float l = sqrtf(o[0]*o[0] + o[1]*o[1] + o[2]*o[2] + o[3]*o[3]); for (int i = 0; i < 4; i++) o[i] /= l;
}
static v3 bpos(t3_object *b) { float p[3]; wpos(b, p); return v3_(p[0], p[1], p[2]); }
/* turn bone b (in world space) by the rotation taking its child's direction onto want, blended by w */
static void aim_bone(t3_object *b, t3_object *child_or_end, v3 from_pt, v3 want_pt, float w) {
  v3 cur = v3_sub(bpos(child_or_end), from_pt), to = v3_sub(want_pt, from_pt);
  if (v3_len(cur) < 1e-4f || v3_len(to) < 1e-4f) return;
  float d[4], wq[4], nw[4], pq[4], pinv[4], loc[4];
  qfrom_to(d, v3_norm(cur), v3_norm(to));
  wquat(b, wq); qmul(nw, d, wq);
  if (b->parent) wquat(b->parent, pq); else { pq[0] = pq[1] = pq[2] = 0; pq[3] = 1; }
  qconj(pinv, pq); qmul(loc, pinv, nw);
  t3_quat L = { loc[0], loc[1], loc[2], loc[3] };
  t3_object_set_quaternion(b, t3_quat_slerp(b->quaternion, L, w));
  t3_object_update_world_matrix(b, false, true);
}
static void apply_reach(actor_t *a, float dt) {
  a->reach_w = ds_damp(a->reach_w, a->reach_t, 8, dt);
  if (a->reach_w < .005f || !a->b_arm[0] || !a->b_arm[2] || !a->b_rhand || !a->b_lhand) return;
  t3_object_update_world_matrix(a->root, true, true);
  for (int s = 0; s < 2; s++) {
    t3_object *up = a->b_arm[s * 2], *lo = a->b_arm[s * 2 + 1], *hand = s ? a->b_lhand : a->b_rhand;
    v3 S = bpos(up), E = bpos(lo), H = bpos(hand), T = a->reach[s];
    float L1 = v3_len(v3_sub(E, S)), L2 = v3_len(v3_sub(H, E));
    v3 dv = v3_sub(T, S); float D = v3_len(dv);
    if (D < 1e-3f) continue;
    v3 dir = v3_scale(dv, 1 / D);
    D = ds_clamp(D, fabsf(L1 - L2) + .01f, L1 + L2 - .002f);
    T = v3_add(S, v3_scale(dir, D));
    /* the elbow hangs down and a little out from the line shoulder to hand */
    v3 out = v3_norm(v3_sub(S, bpos(a->b_chest ? a->b_chest : up)));
    v3 pole = v3_add(v3_(0, -1, 0), v3_scale(out, .6f));
    pole = v3_sub(pole, v3_scale(dir, v3_dot(pole, dir)));
    if (v3_len(pole) < 1e-3f) pole = v3_(0, -1, 0); pole = v3_norm(pole);
    float ax = (L1 * L1 - L2 * L2 + D * D) / (2 * D), h = sqrtf(fmaxf(0, L1 * L1 - ax * ax));
    v3 elbow = v3_add(S, v3_add(v3_scale(dir, ax), v3_scale(pole, h)));
    aim_bone(up, lo, S, elbow, a->reach_w);
    aim_bone(lo, hand, bpos(lo), T, a->reach_w);
  }
}

void actor_update(actor_t *a, float dt) {
  if (!a->gltf_scene || !a->root->visible) return;
  if (a->lod && a->lod->level_count == 3) { float k = lod_scale(); a->lod->levels[1].distance = LOD_NEAR * k; a->lod->levels[2].distance = LOD_FAR * k; }
  mixer_update(&a->mixer, dt);
  if (a->mixer.bound) {}
  apply_hands(a, dt);
  apply_reach(a, dt);
  apply_look(a, dt);
  /* the face */
  a->blink_t -= dt;
  if (a->blink_t < 0) { a->blink_v = 1; a->blink_t = ds_rr(2, 5.5f); }
  a->blink_v = fmaxf(0, a->blink_v - dt * 7);
  float blink = a->blink_v > 0 ? 1 - fabsf(a->blink_v * 2 - 1) : 0;
  for (int i = 0; i < EX_COUNT; i++) a->ex[i] = ds_damp(a->ex[i], a->ex_t[i], 9, dt);
  if (a->face && a->face->morph_influences) {
    float *m = a->face->morph_influences;
    int n = a->face->morph_influence_count;
    float shape = a->mouth_shape, op = ds_clamp(a->mouth, 0, 1);
    /* a(0) e(.25) i(.5) o(.75) u(1) */
    float vow[5] = { fmaxf(0, 1 - fabsf(shape) * 4), fmaxf(0, 1 - fabsf(shape - .25f) * 4), fmaxf(0, 1 - fabsf(shape - .5f) * 4), fmaxf(0, 1 - fabsf(shape - .75f) * 4), fmaxf(0, 1 - fabsf(shape - 1) * 4) };
    float joy = a->ex[EX_JOY];
    if (n > EX_FUN) {
      m[EX_BLINK] = fmaxf(blink * (1 - joy), a->ex[EX_BLINK]);
      m[EX_A] = vow[0] * op; m[EX_E] = vow[1] * op; m[EX_I] = vow[2] * op; m[EX_O] = vow[3] * op; m[EX_U] = vow[4] * op;
      m[EX_JOY] = joy; m[EX_ANGRY] = a->ex[EX_ANGRY]; m[EX_SORROW] = a->ex[EX_SORROW]; m[EX_FUN] = a->ex[EX_FUN];
    }
  }
  t3_object_update_world_matrix(a->root, true, true);
  /* no hair physics on the far body (the last render's level): the springs start over when it comes near */
  if (a->spring_reset) { spring_seed(a); a->spring_reset = false; }
  else if (actor_lod_level(a) >= 2) a->spring_reset = true;
  else if (dt > 0) spring_step(a, dt);
  t3_object_update_world_matrix(a->root, true, true);
}

t3_object *actor_socket_rhand(actor_t *a) { return a->s_rhand; }
t3_object *actor_socket_lhand(actor_t *a) { return a->s_lhand; }
v3 actor_bone_pos(actor_t *a, const char *bone) {
  t3_object *b = a->gltf_scene ? t3_object_get_by_name(a->gltf_scene, bone) : NULL;
  if (!b) return a->pos;
  float p[3]; wpos(b, p);
  return v3_(p[0], p[1], p[2]);
}
v3 actor_head_pos(actor_t *a) { float p[3] = {a->pos.x, a->pos.y + 1.4f, a->pos.z}; if (a->b_head) wpos(a->b_head, p); return v3_(p[0], p[1], p[2]); }
void actor_flash(actor_t *a, float r, float g, float b) {
  t3_material *m[2] = { a->m_face, a->m_body };
  for (int i = 0; i < 2; i++) if (m[i]) m[i]->color = (t3_color){ r, g, b };
}

/* ---- this game's additions ---- */
void actor_attach(actor_t *a, t3_scene *scene) {
  t3_object *o[2] = { a->root, a->shadow ? &a->shadow->base : NULL };
  for (int i = 0; i < 2; i++) {
    if (!o[i]) continue;
    t3_retain(o[i]);
    if (o[i]->parent) t3_object_remove(o[i]->parent, o[i]);
    if (scene) t3_object_add(&scene->base, o[i]);
    t3_release(o[i]);
  }
}
void actor_free(actor_t *a) {
  if (!a) return;
  actor_attach(a, NULL);
  if (a->shadow) t3_release(a->shadow);
  if (a->root) t3_release(a->root);
  if (a->m_body) t3_release(a->m_body);
  if (a->m_face) t3_release(a->m_face);
  if (a->tex) t3_release(a->tex);
  actor_model m = a->model;
  bool had = a->gltf_scene != NULL;
  free(a);
  if (had && TPL[m].users > 0 && --TPL[m].users == 0) unload(m);
}
ck_mixer *actor_mixer(actor_t *a) { return &a->mixer; }
int actor_model_find(const char *letter) {
  char want[16]; snprintf(want, sizeof want, "char_%s.glb", letter);
  for (char *p = want; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
  for (int m = 0; m < AM_COUNT; m++) if (!strcmp(CAST_META[m].glb, want)) return m;
  return -1;
}
const ck_rig *actor_rig(void) { if (!RIG) RIG = rig_load("rig_vroid.bin"); return RIG; }
t3_object *actor_root(actor_t *a) { return a->root; }
t3_object *actor_bone(actor_t *a, const char *name) { return a->gltf_scene ? t3_object_get_by_name(a->gltf_scene, name) : NULL; }
void actor_shadow(actor_t *a, bool on) { if (a->shadow) a->shadow->base.visible = on && a->root->visible; }

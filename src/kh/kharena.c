/* arena.c: the five fight stages (assets/models/arena_*.glb, made by tools/prep-arena.mjs from the
 * lab's own parts). A flat floor, a circle that keeps fighters in, a few round pillars, and props
 * built here: the things near the ring that say where we are, and each stage's second look for a
 * boss's phase two. Mood is colours and the sky and fog, not extra lights. */
#include "kharena.h"
#include "gen/arena_data.h"
#include <stdlib.h>
#include <string.h>

static t3_object *group[5];
static t3_material *mlit, *mglow, *mglass, *mhaze;

/* ── a vertex coloured mesh builder ── */
typedef struct { float *p, *n, *c; uint32_t *i; int nv, ni, cv, ci; } mb_t;
static int mv(mb_t *b, v3 p, v3 n, t3_color c) {
  if (b->nv == b->cv) {
    b->cv = b->cv ? b->cv * 2 : 256;
    b->p = realloc(b->p, sizeof(float) * 3 * (size_t)b->cv); b->n = realloc(b->n, sizeof(float) * 3 * (size_t)b->cv); b->c = realloc(b->c, sizeof(float) * 3 * (size_t)b->cv);
  }
  float *P = b->p + b->nv * 3, *N = b->n + b->nv * 3, *C = b->c + b->nv * 3;
  P[0] = p.x; P[1] = p.y; P[2] = p.z; N[0] = n.x; N[1] = n.y; N[2] = n.z; C[0] = c.r; C[1] = c.g; C[2] = c.b;
  return b->nv++;
}
static void mt(mb_t *b, int a, int c, int d) {
  if (b->ni + 3 > b->ci) { b->ci = b->ci ? b->ci * 2 : 768; b->i = realloc(b->i, sizeof(uint32_t) * (size_t)b->ci); }
  b->i[b->ni++] = (uint32_t)a; b->i[b->ni++] = (uint32_t)c; b->i[b->ni++] = (uint32_t)d;
}
static void quad(mb_t *b, v3 a, v3 c, v3 d, v3 e, t3_color col) {
  v3 n = v3_norm(v3_cross(v3_sub(c, a), v3_sub(e, a)));
  int k = mv(b, a, n, col); mv(b, c, n, col); mv(b, d, n, col); mv(b, e, n, col);
  mt(b, k, k + 1, k + 2); mt(b, k, k + 2, k + 3);
}
/* a box: centre, half sizes, yaw about y. Top a touch lighter, sides a touch darker, as the lab's parts */
static void box(mb_t *b, float x, float y, float z, float hx, float hy, float hz, float yaw, t3_color c) {
  float cs = cosf(yaw), sn = sinf(yaw);
  v3 v[8];
  for (int k = 0; k < 8; k++) {
    float lx = k & 1 ? hx : -hx, ly = k & 2 ? hy : -hy, lz = k & 4 ? hz : -hz;
    v[k] = v3_(x + lx * cs + lz * sn, y + ly, z - lx * sn + lz * cs);
  }
  t3_color top = {c.r * 1.08f, c.g * 1.08f, c.b * 1.08f}, side = {c.r * .9f, c.g * .9f, c.b * .9f};
  quad(b, v[2], v[6], v[7], v[3], top);
  quad(b, v[0], v[1], v[5], v[4], side);
  quad(b, v[4], v[5], v[7], v[6], c);
  quad(b, v[1], v[0], v[2], v[3], c);
  quad(b, v[5], v[1], v[3], v[7], side);
  quad(b, v[0], v[4], v[6], v[2], side);
}
/* a box from its floor footprint up */
static void post(mb_t *b, float x, float z, float hx, float hz, float y0, float y1, float yaw, t3_color c) { box(b, x, (y0 + y1) * .5f, z, hx, (y1 - y0) * .5f, hz, yaw, c); }
/* a spike: four faces to a point */
static void spike(mb_t *b, v3 base, float r, v3 tip, t3_color c) {
  v3 q[4] = {v3_(base.x - r, base.y, base.z), v3_(base.x, base.y, base.z - r), v3_(base.x + r, base.y, base.z), v3_(base.x, base.y, base.z + r)};
  for (int k = 0; k < 4; k++) {
    v3 a = q[k], d = q[(k + 1) % 4], n = v3_norm(v3_cross(v3_sub(d, a), v3_sub(tip, a)));
    if (tip.y < base.y) n = v3_scale(n, -1);
    int i0 = mv(b, a, n, c), i1 = mv(b, d, n, c), i2 = mv(b, tip, n, c);
    if (tip.y < base.y) mt(b, i0, i2, i1); else mt(b, i0, i1, i2);
  }
}
/* a flat ring band facing in, for glow lines */
static void band(mb_t *b, float R, float y0, float y1, int secs, t3_color c) {
  for (int i = 0; i < secs; i++) {
    float a0 = i * 6.2831853f / secs, a1 = (i + 1) * 6.2831853f / secs;
    quad(b, v3_(cosf(a0) * R, y0, sinf(a0) * R), v3_(cosf(a1) * R, y0, sinf(a1) * R), v3_(cosf(a1) * R, y1, sinf(a1) * R), v3_(cosf(a0) * R, y1, sinf(a0) * R), c);
  }
}
/* a horizontal disc of haze */
static void disc(mb_t *b, float R, float y, int secs, t3_color c) {
  int k = mv(b, v3_(0, y, 0), v3_(0, 1, 0), c);
  for (int i = 0; i < secs; i++) { float a = i * 6.2831853f / secs; mv(b, v3_(cosf(a) * R, y, sinf(a) * R), v3_(0, 1, 0), c); }
  for (int i = 0; i < secs; i++) mt(b, k, k + 1 + (i + 1) % secs, k + 1 + i);
}
static t3_mesh *mb_mesh(mb_t *b, t3_material *m, t3_object *parent) {
  if (!b->ni) { free(b->p); free(b->n); free(b->c); free(b->i); memset(b, 0, sizeof *b); return NULL; }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, b->p, b->nv, 3); t3_geometry_set_attribute(g, T3_ATTR_POSITION, a); t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->n, b->nv, 3); t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a); t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->c, b->nv, 3); t3_geometry_set_attribute(g, T3_ATTR_COLOR, a); t3_release(a);
  a = t3_attribute_new(T3_UINT32, b->i, b->ni, 1); t3_geometry_set_index(g, a); t3_release(a);
  t3_geometry_compute_bounding_sphere(g);
  t3_mesh *me = t3_mesh_new(g, m);
  t3_release(g);
  t3_object_add(parent, me); t3_release(me);
  free(b->p); free(b->n); free(b->c); free(b->i); memset(b, 0, sizeof *b);
  return me;
}

/* ── colliders: oriented boxes and posts, per stage, in the base look and in phase two ── */
typedef struct { float x, z, hx, hz, yaw, h; } obox;
typedef struct { float x, z, r; } ocyl;
#define MAXB 24
typedef struct {
  obox b[2][MAXB]; int nb[2];
  ocyl c[2][MAXB]; int nc[2];
  t3_object *look[2];          /* the props of each look */
  t3_object *glb;              /* the baked stage */
  t3_object *chunk[3];         /* its ring chunks a, b, c (the unfolded lab lifts them) */
  t3_mesh *anim;               /* phase two's moving glow (beams, embers) */
} stage_t;
static stage_t S[5];
static int cur = -1, phase;
static float blend;            /* 0 base look .. 1 phase two */
static float clock_s;
static void cb(int st, int ph, float x, float z, float hx, float hz, float yaw, float h) { stage_t *s = &S[st]; if (s->nb[ph] < MAXB) s->b[ph][s->nb[ph]++] = (obox){x, z, hx, hz, yaw, h}; }
static void cc(int st, int ph, float x, float z, float r) { stage_t *s = &S[st]; if (s->nc[ph] < MAXB) s->c[ph][s->nc[ph]++] = (ocyl){x, z, r}; }
/* polar placement on the ring, facing the centre */
static inline float PX(float r, float a) { return cosf(a) * r; }
static inline float PZ(float r, float a) { return sinf(a) * r; }
static inline float TAN(float a) { return -a; }   /* a box's long local x laid along the ring at angle a */

#define H t3_color_hex
static t3_color C(uint32_t h) { return t3_color_hex(h); }

/* a workbench along the ring: top, legs, a shelf, a vice and a lamp arm */
static void workbench(mb_t *b, mb_t *g, int st, int ph, float r, float a) {
  float x = PX(r, a), z = PZ(r, a), y = TAN(a) + 1.5707963f;
  float cs = cosf(y), sn = sinf(y);
  #define LX(lx, lz) (x + (lx) * cs + (lz) * sn)
  #define LZ(lx, lz) (z - (lx) * sn + (lz) * cs)
  box(b, x, .92f, z, 1.6f, .05f, .5f, y, C(0xc29465));
  for (int i = -1; i <= 1; i += 2) for (int k = -1; k <= 1; k += 2) post(b, LX(i * 1.5f, k * .42f), LZ(i * 1.5f, k * .42f), .05f, .05f, 0, .88f, y, C(0x3a3e44));
  box(b, x, .25f, z, 1.5f, .03f, .42f, y, C(0x9d7148));
  post(b, LX(-1.1f, -.3f), LZ(-1.1f, -.3f), .12f, .1f, .97f, 1.15f, y, C(0x5a6066));
  post(b, LX(1.2f, .35f), LZ(1.2f, .35f), .03f, .03f, .97f, 1.7f, y, C(0x2a2d33));
  box(b, LX(1.0f, .35f), 1.72f, LZ(1.0f, .35f), .25f, .03f, .03f, y, C(0x2a2d33));
  if (g) box(g, LX(.78f, .35f), 1.66f, LZ(.78f, .35f), .12f, .04f, .12f, y, C(0xffe0a0));
  post(b, LX(.3f, -.2f), LZ(.3f, -.2f), .25f, .12f, .97f, 1.12f, y, C(0xb02a2a));
  cb(st, ph, x, z, 1.62f, .52f, y, 1.0f);
  #undef LX
  #undef LZ
}
static void stool(mb_t *b, int st, int ph, float r, float a) {
  float x = PX(r, a), z = PZ(r, a);
  post(b, x, z, .2f, .2f, .62f, .7f, a, C(0x24262a));
  post(b, x, z, .04f, .04f, 0, .62f, a, C(0x70747c));
  post(b, x, z, .22f, .22f, 0, .04f, a, C(0x70747c));
  cc(st, ph, x, z, .25f);
}
/* a cabinet with a lit window or door: laser cutters, soda fridges, printers */
static void cabinet(mb_t *b, mb_t *g, int st, int ph, float r, float a, float w, float d, float h, t3_color body, t3_color face, bool lit) {
  float x = PX(r, a), z = PZ(r, a), y = TAN(a) + 1.5707963f;
  post(b, x, z, w, d, 0, h, y, body);
  float cs = cosf(y), sn = sinf(y);
  /* the face toward the centre: local -z */
  float fx = x - sn * (d + .01f), fz = z - cs * (d + .01f);
  box(lit && g ? g : b, fx, h * .55f, fz, w * .8f, h * .32f, .01f, y, face);
  cb(st, ph, x, z, w + .02f, d + .02f, y, h);
}
/* a stack of crates */
static void crates(mb_t *b, int st, int ph, float r, float a, t3_color c) {
  float x = PX(r, a), z = PZ(r, a), y = TAN(a);
  post(b, x, z, .45f, .45f, 0, .9f, y, c);
  post(b, x + .2f, z - .1f, .4f, .4f, .9f, 1.7f, y + .3f, (t3_color){c.r * .85f, c.g * .85f, c.b * .85f});
  cb(st, ph, x, z, .5f, .5f, y, 1.7f);
}
/* a pitch deck slide: a glass pane with a lit slide on it, a title bar and bullet lines */
static void slide(mb_t *gl, mb_t *g, float r, float a, float w, float h, float y0, t3_color accent) {
  float x = PX(r, a), z = PZ(r, a), y = TAN(a) + 1.5707963f;
  float cs = cosf(y), sn = sinf(y);
  box(gl, x, y0 + h * .5f, z, w, h * .5f, .03f, y, C(0xbfe6ff));
  float fx = x - sn * .05f, fz = z - cs * .05f;
  box(g, fx, y0 + h * .82f, fz, w * .86f, h * .07f, .01f, y, accent);
  for (int i = 0; i < 4; i++) {
    float ly = y0 + h * (.62f - i * .14f), lw = w * (.7f - (i % 3) * .14f);
    float ox = -(w * .86f - lw);
    box(g, fx + ox * cs, ly, fz - ox * sn, lw, h * .025f, .01f, y, C(0xf4f8ff));
    box(g, fx + (-w * .8f) * cs, ly, fz - (-w * .8f) * sn, h * .03f, h * .03f, .01f, y, accent);
  }
}

static void build_bench(int st) {
  stage_t *s = &S[st];
  mb_t b = {0}, g = {0}, gl = {0};
  /* base: the long bench's workbenches round the ring, stools, sawhorses with a plank, a tool chest */
  const float WB[4] = {.35f, 1.95f, 3.55f, 5.1f};
  for (int i = 0; i < 4; i++) { workbench(&b, &g, st, 0, 13.6f, WB[i]); stool(&b, st, 0, 12.3f, WB[i] + .1f); stool(&b, st, 0, 12.3f, WB[i] - .12f); }
  for (int i = 0; i < 2; i++) { float a = 1.15f + i * .14f; post(&b, PX(13.4f, a), PZ(13.4f, a), .05f, .4f, 0, .75f, a, C(0x9d7148)); }
  post(&b, PX(13.4f, 1.22f), PZ(13.4f, 1.22f), 1.3f, .18f, .75f, .8f, TAN(1.22f) + 1.5707963f, C(0xd9b07a));
  cb(st, 0, PX(13.4f, 1.22f), PZ(13.4f, 1.22f), 1.3f, .45f, TAN(1.22f) + 1.5707963f, .8f);
  cabinet(&b, NULL, st, 0, 14.2f, 2.75f, .6f, .35f, 1.0f, C(0xb02a2a), C(0x8a1c1c), false);
  cabinet(&b, NULL, st, 0, 14.2f, 4.35f, .5f, .3f, 1.8f, C(0x7a7f62), C(0x5a5e48), false);
  s->look[0] = t3_group_new(); t3_object_add(group[st], s->look[0]);
  mb_mesh(&b, mlit, s->look[0]); mb_mesh(&g, mglow, s->look[0]);
  /* phase two: the pitch deck. Glass lecterns where the benches stood, slides on the ring, a band of
   * slogans overhead, a stage edge of light */
  for (int i = 0; i < 4; i++) {
    float a = WB[i], x = PX(13.6f, a), z = PZ(13.6f, a), y = TAN(a) + 1.5707963f;
    post(&gl, x, z, 1.4f, .45f, 0, 1.05f, y, C(0xcfeeff));
    box(&g, x, 1.07f, z, 1.42f, .02f, .47f, y, C(0x60d0ff));
    cb(st, 1, x, z, 1.45f, .5f, y, 1.05f);
  }
  for (int i = 0; i < 8; i++) {
    float a = i * .785398f + .39f;
    t3_color acc = i % 3 == 0 ? C(0x2a7fff) : i % 3 == 1 ? C(0x00c8b0) : C(0xff6a3d);
    slide(&gl, &g, 18.5f, a, 2.6f, 3.6f, 1.2f + (i % 2) * 1.4f, acc);
  }
  for (int i = 0; i < 24; i++) {
    float a0 = i * .2618f, a1 = a0 + .2f, R = 15.0f, y0 = 8.6f + (i % 2) * .3f;
    quad(&g, v3_(cosf(a0) * R, y0, sinf(a0) * R), v3_(cosf(a1) * R, y0, sinf(a1) * R), v3_(cosf(a1) * R, y0 + .5f, sinf(a1) * R), v3_(cosf(a0) * R, y0 + .5f, sinf(a0) * R), i % 4 ? C(0xe8f4ff) : C(0x2a7fff));
  }
  band(&g, 15.4f, .02f, .1f, 64, C(0x60d0ff));
  band(&g, 12.0f, .01f, .03f, 64, C(0x2a7fff));
  s->look[1] = t3_group_new(); t3_object_add(group[st], s->look[1]);
  mb_mesh(&b, mlit, s->look[1]); mb_mesh(&gl, mglass, s->look[1]); mb_mesh(&g, mglow, s->look[1]);
}

static void build_laser(int st) {
  stage_t *s = &S[st];
  mb_t b = {0}, g = {0}, hz = {0}, an = {0};
  /* base: two laser cutters with lit lids, a scrap rack of ply sheets, a fume extractor */
  cabinet(&b, &g, st, 0, 13.6f, .9f, 1.1f, .55f, 1.05f, C(0x2d2e36), C(0xff3080), true);
  cabinet(&b, &g, st, 0, 13.6f, 2.3f, 1.1f, .55f, 1.05f, C(0x2d2e36), C(0x30c0ff), true);
  for (int k = 0; k < 2; k++) { float a = .9f + k * 1.4f; box(&b, PX(13.6f, a), 1.12f, PZ(13.6f, a), 1.0f, .08f, .5f, TAN(a) + 1.5707963f, C(0x4a1830)); }
  for (int i = 0; i < 6; i++) { float a = 4.0f + i * .05f; post(&b, PX(14.0f, a), PZ(14.0f, a), .03f, .7f, 0, 1.4f + (i % 3) * .2f, TAN(a), i % 2 ? C(0xc8a878) : C(0xb0905e)); }
  cb(st, 0, PX(14.0f, 4.12f), PZ(14.0f, 4.12f), .5f, .8f, TAN(4.12f), 1.8f);
  post(&b, PX(13.8f, 5.4f), PZ(13.8f, 5.4f), .35f, .35f, 0, 1.6f, 0, C(0x70747c));
  post(&b, PX(13.8f, 5.4f), PZ(13.8f, 5.4f), .1f, .1f, 1.6f, 3.2f, 0, C(0x70747c));
  cc(st, 0, PX(13.8f, 5.4f), PZ(13.8f, 5.4f), .45f);
  s->look[0] = t3_group_new(); t3_object_add(group[st], s->look[0]);
  mb_mesh(&b, mlit, s->look[0]); mb_mesh(&g, mglow, s->look[0]);
  /* phase two: chlorine. Green haze in layers, the gantry's rails and beams lit hot, warning beacons */
  for (int z = -1; z <= 1; z += 2) {
    box(&g, 0, 9.45f, z * 15.5f, 19.05f, .06f, .52f, 0, C(0x60ff90));
    box(&g, 0, 8.45f, z * 15.5f, 19.05f, .06f, .52f, 0, C(0xff2a80));
    for (int x = -1; x <= 1; x += 2) box(&g, x * 18, 4.25f, z * 15.5f - z * .62f, .4f, 4.2f, .02f, 0, C(0x60ff90));
  }
  box(&g, -1.9f, 10.45f, 0, .72f, .05f, 15, 0, C(0x60ff90)); box(&g, 1.9f, 10.45f, 0, .72f, .05f, 15, 0, C(0x60ff90));
  for (int i = 0; i < 4; i++) { float a = i * 1.5707963f + .785f; post(&g, PX(14.9f, a), PZ(14.9f, a), .18f, .18f, 2.6f, 3.0f, 0, C(0x90ff40)); post(&b, PX(14.9f, a), PZ(14.9f, a), .06f, .06f, 0, 2.6f, 0, C(0x2a2d33)); cc(st, 1, PX(14.9f, a), PZ(14.9f, a), .2f); }
  /* the same cutters and rack stay solid in phase two */
  for (int i = 0; i < S[st].nb[0]; i++) S[st].b[1][S[st].nb[1]++] = S[st].b[0][i];
  for (int i = 0; i < S[st].nc[0]; i++) S[st].c[1][S[st].nc[1]++] = S[st].c[0][i];
  for (int k = 0; k < 2; k++) disc(&hz, 36, .12f + k * 3.4f, 24, k % 2 ? C(0x4cff60) : C(0x9cff40));
  /* sweeping beams from the gantry: four thin glow posts, moved in kharena_update */
  for (int i = 0; i < 4; i++) post(&an, (i - 1.5f) * 8, (i % 2 ? -1 : 1) * 15.5f, .06f, .06f, 0, 8.4f, 0, i % 2 ? C(0x60ff90) : C(0xff2a80));
  s->look[1] = t3_group_new(); t3_object_add(group[st], s->look[1]);
  mb_mesh(&b, mlit, s->look[1]); mb_mesh(&g, mglow, s->look[1]);
  t3_mesh *h = mb_mesh(&hz, mhaze, s->look[1]); if (h) ((t3_object *)h)->render_order = 10;
  s->anim = mb_mesh(&an, mglow, s->look[1]);
}

static void build_fridge(int st) {
  stage_t *s = &S[st];
  mb_t b = {0}, g = {0}, gl = {0}, ice = {0};
  /* base: soda fridges with lit doors, a counter with a sink, stacked crates of soda */
  cabinet(&b, &g, st, 0, 14.0f, .4f, .55f, .4f, 2.0f, C(0xe8eef2), C(0xbfe8ff), true);
  cabinet(&b, &g, st, 0, 14.0f, .5f, .55f, .4f, 2.0f, C(0xd03030), C(0xffe8e0), true);
  { float a = 2.2f, x = PX(13.8f, a), z = PZ(13.8f, a), y = TAN(a) + 1.5707963f;
    post(&b, x, z, 1.8f, .4f, 0, .9f, y, C(0xdfe9f0)); box(&b, x, .93f, z, 1.85f, .03f, .45f, y, C(0x8aa0b0)); box(&b, x, .9f, z, .4f, .05f, .3f, y, C(0x9ab0c0));
    cb(st, 0, x, z, 1.85f, .45f, y, .95f); }
  crates(&b, st, 0, 13.6f, 3.5f, C(0x2a6fd0)); crates(&b, st, 0, 13.6f, 3.62f, C(0xd03030)); crates(&b, st, 0, 13.6f, 5.0f, C(0x30a050));
  s->look[0] = t3_group_new(); t3_object_add(group[st], s->look[0]);
  mb_mesh(&b, mlit, s->look[0]); mb_mesh(&g, mglow, s->look[0]);
  /* phase two: frozen. The same fridges and counter under ice, spikes round the ring and a few
   * blocks of ice in the floor to fight round, icicles from the walls, frost over everything */
  for (int i = 0; i < S[st].nb[0]; i++) {
    obox q = S[st].b[0][i];
    post(&ice, q.x, q.z, q.hx + .12f, q.hz + .12f, 0, q.h + .25f, q.yaw, C(0xe4f6ff));
    S[st].b[1][S[st].nb[1]++] = (obox){q.x, q.z, q.hx + .12f, q.hz + .12f, q.yaw, q.h + .25f};
  }
  const float BL[5][3] = {{7.5f, .6f, 1.1f}, {8.5f, 2.6f, .8f}, {6.8f, 4.1f, 1.2f}, {9.0f, 5.5f, .9f}, {4.0f, 3.3f, .7f}};
  for (int i = 0; i < 5; i++) {
    float x = PX(BL[i][0], BL[i][1]), z = PZ(BL[i][0], BL[i][1]), r = BL[i][2];
    post(&ice, x, z, r, r * .8f, 0, 1.4f + r, BL[i][1], C(0xd4eefc));
    spike(&ice, v3_(x, 1.4f + r, z), r * .7f, v3_(x + .2f, 2.6f + r * 1.4f, z - .1f), C(0xf4fbff));
    cb(st, 1, x, z, r + .05f, r * .8f + .05f, BL[i][1], 2.4f + r);
  }
  for (int i = 0; i < 40; i++) {
    float a = i * .157f + (i % 3) * .03f, r = 15.6f + (i % 4) * .5f, h = 1.0f + (i * 37 % 11) * .16f;
    spike(&ice, v3_(PX(r, a), 0, PZ(r, a)), .35f + (i % 3) * .15f, v3_(PX(r - .6f, a), h, PZ(r - .6f, a)), i % 2 ? C(0xe8f8ff) : C(0xc4e4f8));
  }
  for (int i = 0; i < 60; i++) {
    float a = i * .1047f, r = 35.5f, y = 9.0f, l = 1.0f + (i * 13 % 7) * .45f;
    spike(&ice, v3_(PX(r, a), y, PZ(r, a)), .3f, v3_(PX(r - .2f, a), y - l, PZ(r - .2f, a)), C(0xf0faff));
  }
  for (int i = 0; i < 9; i++) { float a = i * .7f, r = 2 + i * 1.2f; quad(&gl, v3_(PX(r, a) - 2, .03f, PZ(r, a) + 1.5f), v3_(PX(r, a) + 2, .03f, PZ(r, a) + 1.8f), v3_(PX(r, a) + 1.6f, .03f, PZ(r, a) - 1.6f), v3_(PX(r, a) - 1.8f, .03f, PZ(r, a) - 1.4f), C(0xffffff)); }
  band(&g, 15.5f, .02f, .12f, 64, C(0x9fe0ff));
  s->look[1] = t3_group_new(); t3_object_add(group[st], s->look[1]);
  mb_mesh(&ice, mlit, s->look[1]); mb_mesh(&g, mglow, s->look[1]);
  t3_mesh *h = mb_mesh(&gl, mhaze, s->look[1]); if (h) ((t3_object *)h)->render_order = 10;
}

static void build_shop(int st) {
  stage_t *s = &S[st];
  mb_t b = {0}, g = {0}, old = {0}, og = {0}, sh = {0};
  /* base: a drill press, a welding cart, a steel rack, between the piers */
  for (int i = 0; i < 6; i++) {
    float a = i * 1.0471976f + .26f + .52f;
    if (i % 3 == 0) {        /* drill press */
      float x = PX(13.2f, a), z = PZ(13.2f, a);
      post(&b, x, z, .35f, .3f, 0, .12f, a, C(0x383c42)); post(&b, x, z, .07f, .07f, 0, 1.7f, a, C(0x5a6066));
      box(&b, x, 1.75f, z, .3f, .2f, .45f, a, C(0x2f6f4a)); box(&b, x, .95f, z, .25f, .03f, .25f, a, C(0x5a6066));
      cc(st, 0, x, z, .4f);
    } else if (i % 3 == 1) { /* welding cart */
      float x = PX(13.2f, a), z = PZ(13.2f, a);
      post(&b, x, z, .4f, .3f, 0, .9f, a, C(0x2a4a8a)); post(&b, x - .1f, z, .12f, .12f, .9f, 1.5f, a, C(0x70747c));
      box(&g, x, .93f, z, .2f, .03f, .15f, a, C(0xff8a30));
      cb(st, 0, x, z, .42f, .32f, a, 1.5f);
    } else {                 /* steel rack */
      float x = PX(13.6f, a), z = PZ(13.6f, a), y = TAN(a) + 1.5707963f;
      for (int k = -1; k <= 1; k += 2) post(&b, x + cosf(y) * k * .9f, z - sinf(y) * k * .9f, .04f, .3f, 0, 2.2f, y, C(0xc8611c));
      for (int k = 0; k < 4; k++) box(&b, x, .3f + k * .6f, z, .95f, .03f, .3f, y, C(0x70747c));
      cb(st, 0, x, z, 1.0f, .35f, y, 2.2f);
    }
  }
  s->look[0] = t3_group_new(); t3_object_add(group[st], s->look[0]);
  mb_mesh(&b, mlit, s->look[0]); mb_mesh(&g, mglow, s->look[0]);
  /* phase two: the old building. Brick walls close in at 27 m and rise high, timber posts on the ring, roof
   * trusses, bulbs on cords and sun through high windows: warmer, older, bigger */
  for (int i = 0; i < S[st].nc[0]; i++) S[st].c[1][S[st].nc[1]++] = S[st].c[0][i];
  for (int i = 0; i < S[st].nb[0]; i++) S[st].b[1][S[st].nb[1]++] = S[st].b[0][i];
  const float WR = 27.f;
  for (int i = 0; i < 40; i++) {
    float a0 = i * .15708f, a1 = a0 + .15708f;
    for (int k = 0; k < 9; k++) {
      float y0 = k * 2.f, y1 = y0 + 2.f;
      t3_color c = k == 2 ? C(0x8a5a30) : (i + k) % 3 ? C(0x9a4a32) : C(0x8a3e2a);
      if (k >= 4 && k <= 6 && i % 4 == 1) { c = C(0xffd9a0); quad(&og, v3_(cosf(a0) * (WR - .02f), y0, sinf(a0) * (WR - .02f)), v3_(cosf(a1) * (WR - .02f), y0, sinf(a1) * (WR - .02f)), v3_(cosf(a1) * (WR - .02f), y1, sinf(a1) * (WR - .02f)), v3_(cosf(a0) * (WR - .02f), y1, sinf(a0) * (WR - .02f)), c); continue; }
      quad(&old, v3_(cosf(a0) * WR, y0, sinf(a0) * WR), v3_(cosf(a1) * WR, y0, sinf(a1) * WR), v3_(cosf(a1) * WR, y1, sinf(a1) * WR), v3_(cosf(a0) * WR, y1, sinf(a0) * WR), c);
    }
  }
  for (int i = 0; i < 12; i++) {
    float a = i * .5235988f;
    post(&old, PX(19.5f, a), PZ(19.5f, a), .35f, .35f, 0, 15, a, C(0x6a4a2c));
    box(&old, PX(19.5f, a) * .5f, 15.2f, PZ(19.5f, a) * .5f, 9.9f, .25f, .22f, -a, C(0x5a3e24));
    box(&og, PX(9.0f, a + .26f), 9.5f, PZ(9.0f, a + .26f), .16f, .16f, .16f, 0, C(0xffc870));
    post(&old, PX(9.0f, a + .26f), PZ(9.0f, a + .26f), .01f, .01f, 9.6f, 15, 0, C(0x1a1410));
  }
  for (int i = 0; i < 6; i++) { float a = i * 1.0471976f; box(&old, 0, 15.8f + (i % 2) * .4f, 0, WR, .2f, .3f, a, C(0x4e3620)); }
  for (int i = 0; i < 5; i++) {
    float a = 2.4f + i * .3f, x0 = PX(WR - .5f, a), z0 = PZ(WR - .5f, a), x1 = PX(4.f, a + .4f), z1 = PZ(4.f, a + .4f);
    quad(&sh, v3_(x0 - .9f, 11.5f, z0), v3_(x0 + .9f, 11.5f, z0), v3_(x1 + 1.2f, 0.05f, z1), v3_(x1 - 1.2f, 0.05f, z1), C(0xffc070));
  }
  for (int i = 0; i < 64; i++) { float a = i * .0982f, r = 6 + (i * 37 % 19); quad(&old, v3_(PX(r, a) - .5f, .015f, PZ(r, a) - .2f), v3_(PX(r, a) + .5f, .015f, PZ(r, a) - .2f), v3_(PX(r, a) + .5f, .015f, PZ(r, a) + .2f), v3_(PX(r, a) - .5f, .015f, PZ(r, a) + .2f), C(0x6a5034)); }
  for (int i = 0; i < 12; i++) cc(st, 1, PX(19.5f, i * .5235988f), PZ(19.5f, i * .5235988f), .4f);
  s->look[1] = t3_group_new(); t3_object_add(group[st], s->look[1]);
  mb_mesh(&b, mlit, s->look[1]); mb_mesh(&old, mlit, s->look[1]); mb_mesh(&og, mglow, s->look[1]);
  t3_mesh *h = mb_mesh(&sh, mhaze, s->look[1]); if (h) ((t3_object *)h)->render_order = 10;
}

static void build_unfold(int st) {
  stage_t *s = &S[st];
  mb_t b = {0}, g = {0}, fr = {0}, gl = {0};
  /* base: bits of the lab dropped on the disc near the ring: the lounge couch, a printer, a soda fridge,
   * the welcome table, a workbench */
  { float a = .3f, x = PX(30.5f, a), z = PZ(30.5f, a), y = TAN(a) + 1.5707963f;
    post(&b, x, z, 1.4f, .45f, 0, .45f, y, C(0x6a3a7a)); post(&b, x + sinf(y) * .4f, z + cosf(y) * .4f, 1.4f, .12f, .45f, .95f, y, C(0x5a2e6a));
    cb(st, 0, x, z, 1.45f, .55f, y, .95f); }
  cabinet(&b, &g, st, 0, 30.5f, 1.4f, .3f, .3f, .55f, C(0x2e3034), C(0x96a0aa), false);
  cabinet(&b, &g, st, 0, 30.5f, 2.6f, .5f, .4f, 1.9f, C(0xd03030), C(0xffe8e0), true);
  workbench(&b, &g, st, 0, 30.5f, 3.8f);
  { float a = 5.1f, x = PX(30.5f, a), z = PZ(30.5f, a), y = TAN(a) + 1.5707963f;
    post(&b, x, z, 1.0f, .4f, 0, .75f, y, C(0xe6e2d4)); box(&g, x, .78f, z, .3f, .01f, .2f, y, C(0xd9702a)); cb(st, 0, x, z, 1.05f, .45f, y, .78f); }
  s->look[0] = t3_group_new(); t3_object_add(group[st], s->look[0]);
  mb_mesh(&b, mlit, s->look[0]); mb_mesh(&g, mglow, s->look[0]);
  /* phase two: the disc's edge breaks into slabs that float off, the rooms lift away (kharena_update moves
   * the baked ring chunks), a ring of light under the floor */
  for (int i = 0; i < 36; i++) {
    float a = i * .1745f + (i % 3) * .05f, r = 58 + (i * 29 % 13), y = -6 + (i * 17 % 11) * 1.6f, w = 1.5f + (i % 4) * .9f;
    post(&fr, PX(r, a), PZ(r, a), w, w * .7f, y, y + .8f + (i % 3) * .4f, a * 3, (i % 2) ? C(0xe8e1d0) : C(0xd6cdb6));
    post(&fr, PX(r, a), PZ(r, a), w * .8f, w * .55f, y - 1.6f, y, a * 3, C(0x2a2636));
  }
  for (int i = 0; i < 24; i++) { float a = i * .2618f; quad(&gl, v3_(PX(57, a), -3, PZ(57, a)), v3_(PX(57, a + .2618f), -3, PZ(57, a + .2618f)), v3_(PX(57, a + .2618f), 0, PZ(57, a + .2618f)), v3_(PX(57, a), 0, PZ(57, a)), C(0x8a60ff)); }
  band(&g, 33.2f, .02f, .14f, 96, C(0xb090ff));
  for (int i = 0; i < S[st].nb[0]; i++) S[st].b[1][S[st].nb[1]++] = S[st].b[0][i];
  s->look[1] = t3_group_new(); t3_object_add(group[st], s->look[1]);
  s->anim = mb_mesh(&fr, mlit, s->look[1]);
  mb_mesh(&g, mglow, s->look[1]);
  t3_mesh *h = mb_mesh(&gl, mhaze, s->look[1]); if (h) ((t3_object *)h)->render_order = 10;
}

void kharena_init(t3_object *root) {
  mlit = t3_mesh_lambert_material_new(0xffffff); mlit->vertex_colors = true; mlit->side = T3_DOUBLE_SIDE;
  mglow = t3_mesh_basic_material_new(0xffffff); mglow->vertex_colors = true; mglow->side = T3_DOUBLE_SIDE;
  mglow->color = (t3_color){1.6f, 1.6f, 1.6f};
  mglass = t3_mesh_basic_material_new(0xffffff); mglass->vertex_colors = true; mglass->transparent = true; mglass->opacity = .32f; mglass->side = T3_DOUBLE_SIDE; mglass->depth_write = false;
  mhaze = t3_mesh_basic_material_new(0xffffff); mhaze->vertex_colors = true; mhaze->transparent = true; mhaze->opacity = .08f; mhaze->side = T3_DOUBLE_SIDE; mhaze->depth_write = false;
  for (int i = 0; i < 5; i++) {
    group[i] = t3_group_new();
    t3_object_add(root, group[i]);
    size_t n; uint8_t *b = ds_asset(ARENA_INFO[i].glb, &n);
    if (b) {
      t3_gltf *g = t3_gltf_parse(b, n, NULL, NULL);
      free(b);
      if (g) {
        t3_object *r = g->scene;
        for (int k = 0; k < r->child_count; k++) {
          t3_object *o = r->children[k];
          t3_mesh *m = o->type == T3_MESH ? (t3_mesh *)o : o->child_count && o->children[0]->type == T3_MESH ? (t3_mesh *)o->children[0] : NULL;
          if (!m) continue;
          t3_material *nm = o->name && !strncmp(o->name, "glow_", 5) ? mglow : mlit;
          t3_retain(nm); t3_release(m->materials[0]); m->materials[0] = nm;
          size_t L = o->name ? strlen(o->name) : 0;
          if (L > 2 && o->name[L - 2] == '_' && o->name[L - 1] >= 'a' && o->name[L - 1] <= 'c') S[i].chunk[o->name[L - 1] - 'a'] = o;
        }
        t3_object_add(group[i], r);
        S[i].glb = r;
      }
    }
    group[i]->visible = false;
  }
  build_bench(0); build_laser(1); build_fridge(2); build_shop(3); build_unfold(4);
  for (int i = 0; i < 5; i++) { if (S[i].look[1]) S[i].look[1]->visible = false; }
}
static inline int idx(world_stage s) { return (int)s - (int)STAGE_ARENA_BENCH; }
void kharena_show(world_stage s) {
  for (int i = 0; i < 5; i++) if (group[i]) group[i]->visible = (i == idx(s));
  cur = s == STAGE_TOWN ? -1 : idx(s);
  kharena_set_phase(0); blend = 0;
  kharena_update(0);
}
void kharena_set_phase(int p) { phase = p ? 1 : 0; }
int kharena_get_phase(void) { return phase; }
float kharena_blend(void) { return blend; }
void kharena_floor(world_stage s, v3 *c, float *r) { *c = v3_(0, 0, 0); *r = ARENA_INFO[idx(s)].clear; }

/* per frame: the change of look grows in and out, and phase two's moving parts move */
bool kharena_update(float dt) {
  if (cur < 0) return false;
  stage_t *s = &S[cur];
  clock_s += dt;
  float nb = ds_clamp(blend + (phase ? dt : -dt) / 1.6f, 0, 1);
  bool changed = nb != blend || dt == 0;
  blend = nb;
  float e = blend * blend * (3 - 2 * blend);
  /* the looks: the old one sinks into the floor, the new one rises from it */
  if (s->look[0]) { s->look[0]->visible = e < .999f; s->look[0]->scale = t3_v3(1, fmaxf(.001f, 1 - e), 1); }
  if (s->look[1]) { s->look[1]->visible = e > .001f; s->look[1]->scale = t3_v3(1, fmaxf(.001f, e), 1); }
  if (cur == 1 && s->anim) {          /* the laser: beams sweep across the ring */
    float x = sinf(clock_s * .9f) * 15;
    t3_object_set_position(s->anim, x * .3f, 0, 0);
    t3_object_set_rotation(s->anim, 0, sinf(clock_s * .4f) * .5f, 0);
  }
  if (cur == 4) {                     /* the unfolded lab: rooms lift and drift, slabs turn slowly */
    for (int k = 0; k < 3; k++) if (s->chunk[k]) {
      float lift = e * (5 + k * 4) + e * sinf(clock_s * (.5f + k * .17f) + k * 2) * 1.2f;
      t3_object_set_position(s->chunk[k], 0, lift, 0);
      t3_object_set_rotation(s->chunk[k], 0, e * sinf(clock_s * .05f + k) * .12f, 0);
    }
    if (s->anim) { t3_object_set_rotation(s->anim, 0, clock_s * .02f, 0); t3_object_set_position(s->anim, 0, sinf(clock_s * .3f) * .8f, 0); }
  }
  return changed;
}

/* ── collision ── */
static bool push_box(const obox *q, v3 *p, float r, v3 *n) {
  if (p->y > q->h - .05f) return false;
  float cs = cosf(q->yaw), sn = sinf(q->yaw), dx = p->x - q->x, dz = p->z - q->z;
  float lx = dx * cs - dz * sn, lz = dx * sn + dz * cs;           /* inverse of the build's rotation */
  float cx = ds_clamp(lx, -q->hx, q->hx), cz = ds_clamp(lz, -q->hz, q->hz), ex = lx - cx, ez = lz - cz, d2 = ex * ex + ez * ez;
  if (d2 >= r * r) return false;
  float nx, nz;
  if (d2 > 1e-8f) { float d = sqrtf(d2); nx = ex / d; nz = ez / d; lx = cx + nx * r; lz = cz + nz * r; }
  else { float ox = q->hx - fabsf(lx), oz = q->hz - fabsf(lz); if (ox < oz) { nx = lx < 0 ? -1 : 1; nz = 0; lx = nx * (q->hx + r); } else { nz = lz < 0 ? -1 : 1; nx = 0; lz = nz * (q->hz + r); } }
  p->x = q->x + lx * cs + lz * sn; p->z = q->z - lx * sn + lz * cs;
  *n = v3_(nx * cs + nz * sn, 0, -nx * sn + nz * cs);
  return true;
}
static int live_phase(void) { return blend > .5f ? 1 : 0; }
void kharena_push(world_stage s, v3 *p, float r, float h, v3 *n, bool *hit) {
  const arena_info *A = &ARENA_INFO[idx(s)];
  const stage_t *T = &S[idx(s)];
  (void)h;
  float d = hypotf(p->x, p->z), lim = A->keep - r;
  if (d > lim && d > 1e-4f) { p->x *= lim / d; p->z *= lim / d; *n = v3_(-p->x / lim, 0, -p->z / lim); *hit = true; }
  for (int i = 0; i < A->np; i++) {
    float dx = p->x - A->p[i].x, dz = p->z - A->p[i].z, q = hypotf(dx, dz), m = A->p[i].r + r;
    if (q < m && q > 1e-4f) { p->x = A->p[i].x + dx / q * m; p->z = A->p[i].z + dz / q * m; *n = v3_(dx / q, 0, dz / q); *hit = true; }
  }
  int ph = live_phase();
  for (int i = 0; i < T->nc[ph]; i++) {
    const ocyl *c = &T->c[ph][i];
    float dx = p->x - c->x, dz = p->z - c->z, q = hypotf(dx, dz), m = c->r + r;
    if (q < m && q > 1e-4f) { p->x = c->x + dx / q * m; p->z = c->z + dz / q * m; *n = v3_(dx / q, 0, dz / q); *hit = true; }
  }
  for (int i = 0; i < T->nb[ph]; i++) if (push_box(&T->b[ph][i], p, r, n)) *hit = true;
}
static bool ray_obox(const obox *q, v3 o, v3 d, float *t, v3 *n) {
  float cs = cosf(q->yaw), sn = sinf(q->yaw);
  float ox = o.x - q->x, oz = o.z - q->z;
  v3 lo = v3_(ox * cs - oz * sn, o.y, ox * sn + oz * cs), ld = v3_(d.x * cs - d.z * sn, d.y, d.x * sn + d.z * cs);
  const float mn[3] = {-q->hx, 0, -q->hz}, mx[3] = {q->hx, q->h, q->hz}, oo[3] = {lo.x, lo.y, lo.z}, dd[3] = {ld.x, ld.y, ld.z};
  float t0 = 0, t1 = 1e9f; int ax = -1; float sg = 1;
  for (int a = 0; a < 3; a++) {
    if (fabsf(dd[a]) < 1e-8f) { if (oo[a] < mn[a] || oo[a] > mx[a]) return false; continue; }
    float u0 = (mn[a] - oo[a]) / dd[a], u1 = (mx[a] - oo[a]) / dd[a], s = -1;
    if (u0 > u1) { float k = u0; u0 = u1; u1 = k; s = 1; }
    if (u0 > t0) { t0 = u0; ax = a; sg = s; }
    if (u1 < t1) t1 = u1;
    if (t0 > t1) return false;
  }
  if (ax < 0) return false;
  *t = t0;
  v3 ln = ax == 0 ? v3_(sg, 0, 0) : ax == 1 ? v3_(0, sg, 0) : v3_(0, 0, sg);
  *n = v3_(ln.x * cs + ln.z * sn, ln.y, -ln.x * sn + ln.z * cs);
  return true;
}
bool kharena_raycast(world_stage s, v3 o, v3 d, float max_dist, float *t_out, v3 *n_out) {
  const arena_info *A = &ARENA_INFO[idx(s)];
  const stage_t *T = &S[idx(s)];
  float best = max_dist; v3 bn = v3_(0, 1, 0); bool any = false;
  if (d.y < -1e-6f) { float t = -o.y / d.y; if (t >= 0 && t < best) { best = t; bn = v3_(0, 1, 0); any = true; } }
  /* the cylinder of the keep-in ring and past it, seen from inside: a wall 4 m beyond it */
  float R = A->keep + 4, a = d.x * d.x + d.z * d.z, b = o.x * d.x + o.z * d.z, c = o.x * o.x + o.z * o.z - R * R;
  if (a > 1e-7f && c < 0) { float t = (-b + sqrtf(b * b - a * c)) / a; if (t >= 0 && t < best) { best = t; v3 q = v3_add(o, v3_scale(d, t)); bn = v3_norm(v3_(-q.x, 0, -q.z)); any = true; } }
  int ph = live_phase();
  for (int i = 0; i < A->np + T->nc[ph]; i++) {
    float px = i < A->np ? A->p[i].x : T->c[ph][i - A->np].x, pz = i < A->np ? A->p[i].z : T->c[ph][i - A->np].z, r = i < A->np ? A->p[i].r : T->c[ph][i - A->np].r;
    float ox = o.x - px, oz = o.z - pz, bb = ox * d.x + oz * d.z, cc2 = ox * ox + oz * oz - r * r;
    if (a < 1e-7f) continue;
    float disc2 = bb * bb - a * cc2;
    if (disc2 < 0) continue;
    float t = (-bb - sqrtf(disc2)) / a;
    float top = i < A->np ? 7 : 3;
    if (t >= 0 && t < best && o.y + d.y * t < top) { best = t; v3 q = v3_add(o, v3_scale(d, t)); bn = v3_norm(v3_(q.x - px, 0, q.z - pz)); any = true; }
  }
  for (int i = 0; i < T->nb[ph]; i++) { float t; v3 n; if (ray_obox(&T->b[ph][i], o, d, &t, &n) && t < best) { best = t; bn = n; any = true; } }
  if (!any) return false;
  *t_out = best; if (n_out) *n_out = bn;
  return true;
}

/* ── mood ── */
static void mood_base(int s, kharena_mood *m) {
  switch (s) {
    default:
    case 0:  /* late sun through a sawdust haze */
      *m = (kharena_mood){H(0x3a2c52), H(0xf0a060), H(0xc08860), H(0xffe6c4), H(0x7c5a40), H(0xffb868), 1.7f, 1.7f, 26, 90, v3_norm(v3_(-.7f, .45f, .4f)), 0}; break;
    case 1:  /* magenta and cyan in the dark */
      *m = (kharena_mood){H(0x0c0a24), H(0x5a1050), H(0x2c0c34), H(0xc0a0ff), H(0xa02060), H(0xff60c0), 1.5f, 1.3f, 22, 80, v3_norm(v3_(.3f, .9f, -.3f)), .6f}; break;
    case 2:  /* cold and bright */
      *m = (kharena_mood){H(0x8cc4e8), H(0xe8f6ff), H(0xd8ecf8), H(0xdceaf8), H(0x90b0cc), H(0xc8e4ff), 1.15f, .6f, 24, 85, v3_norm(v3_(.2f, 1, .3f)), 0}; break;
    case 3:  /* furnace orange on steel */
      *m = (kharena_mood){H(0x14100e), H(0x70300c), H(0x3a1c0c), H(0xd8c0a8), H(0x904820), H(0xff8a30), 1.5f, 2.0f, 22, 80, v3_norm(v3_(.5f, .6f, -.6f)), 0}; break;
    case 4:  /* the night sky */
      *m = (kharena_mood){H(0x05061a), H(0x241c50), H(0x181438), H(0x98a0e8), H(0x484068), H(0xc8c0f0), 1.2f, .8f, 70, 190, v3_norm(v3_(-.4f, .8f, .4f)), 1}; break;
  }
}
static void mood_two(int s, kharena_mood *m) {
  switch (s) {
    default:
    case 0:  /* the pitch deck: a white stage, a cold corporate sky */
      *m = (kharena_mood){H(0x2a5cc8), H(0xe8f2ff), H(0xd4e4f8), H(0xf4f8ff), H(0x8aa4c8), H(0xffffff), 1.9f, 1.1f, 30, 110, v3_norm(v3_(.1f, 1, .2f)), 0}; break;
    case 1:  /* chlorine: the cut PVC fogs the corner green */
      *m = (kharena_mood){H(0x041208), H(0x2e6a1c), H(0x1e4416), H(0xc8ffd0), H(0x305a30), H(0x80ff60), 1.5f, 1.2f, 16, 80, v3_norm(v3_(.3f, .9f, -.3f)), .15f}; break;
    case 2:  /* frozen over: white out */
      *m = (kharena_mood){H(0x9ccff0), H(0xeaf6ff), H(0xdcecf8), H(0xffffff), H(0xa8c8e0), H(0xe8f6ff), 1.3f, .6f, 14, 80, v3_norm(v3_(.2f, 1, .3f)), 0}; break;
    case 3:  /* the old building: sepia dust and afternoon sun through high windows */
      *m = (kharena_mood){H(0x3a2614), H(0xd8a060), H(0xb08050), H(0xffe0b8), H(0x7a5030), H(0xffc070), 1.6f, 1.9f, 16, 70, v3_norm(v3_(-.7f, .5f, .3f)), 0}; break;
    case 4:  /* broken apart: deeper night, more stars, a violet glow from under the disc */
      *m = (kharena_mood){H(0x020210), H(0x3a1870), H(0x140c30), H(0xa898ff), H(0x6040a0), H(0xd8c8ff), 1.15f, .7f, 80, 230, v3_norm(v3_(-.2f, .9f, .5f)), 1.4f}; break;
  }
}
static t3_color cmix(t3_color a, t3_color b, float t) { return (t3_color){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
void kharena_mood_of(world_stage s, kharena_mood *m) {
  int i = idx(s);
  mood_base(i, m);
  if (i != cur || blend <= 0) return;
  kharena_mood q; mood_two(i, &q);
  float t = blend * blend * (3 - 2 * blend);
  m->zenith = cmix(m->zenith, q.zenith, t); m->horizon = cmix(m->horizon, q.horizon, t); m->fog = cmix(m->fog, q.fog, t);
  m->hemi_sky = cmix(m->hemi_sky, q.hemi_sky, t); m->hemi_ground = cmix(m->hemi_ground, q.hemi_ground, t); m->sun = cmix(m->sun, q.sun, t);
  m->hemi += (q.hemi - m->hemi) * t; m->dir += (q.dir - m->dir) * t; m->fog_near += (q.fog_near - m->fog_near) * t; m->fog_far += (q.fog_far - m->fog_far) * t;
  m->sun_dir = v3_norm(v3_lerp(m->sun_dir, q.sun_dir, t)); m->stars += (q.stars - m->stars) * t;
}

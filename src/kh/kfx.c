/* kfx.c: KEYHOLDER's src/fx.c (see kfx.h), renamed kfx_ beside this game's own fx.c. Instanced
 * rings of particles, rewritten once a frame. Here every stage's floor is at y 0 (stage.c puts
 * the ring there), and the pools can move to another scene (kfx_attach). */
#include "kfx.h"
#include <stdlib.h>
#include <string.h>

enum { K_BILL, K_STREAK, K_LINE, K_FLAT, K_RINGB, K_SMOKE };

typedef struct {
  v3 p, v, p2;
  float life, max, s0, s1, w0, w1, rot, grav, drag, fy, bright;
  float c[3];
  uint8_t kind, bounce;
} part_t;

typedef struct { t3_instanced_mesh *im; part_t *p; int cap, next; } pool_t;

static pool_t glow, ring, smoke;
static v3 cam_r, cam_u, cam_p;

static pool_t pool_make(t3_scene *sc, t3_geometry *g, t3_material *m, int cap) {
  pool_t P = { t3_instanced_mesh_new(g, m, cap), calloc(cap, sizeof(part_t)), cap, 0 };
  t3_instanced_mesh_set_color_at(P.im, 0, t3_color_hex(0xffffff));
  t3_object *o = (t3_object *)P.im;
  o->frustum_culled = false; o->cast_shadow = false; o->receive_shadow = false;
  o->render_order = 5;
  P.im->count = 0; o->visible = false;
  t3_object_add(&sc->base, P.im);
  t3_release(g); t3_release(m);
  return P;
}

static t3_texture *tex_make(int shape) {   /* 0 soft glow, 1 thin ring, 2 alpha puff */
  enum { N = 96 };
  static uint8_t px[N * N * 4];
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
    float dx = (x + .5f) / N * 2 - 1, dy = (y + .5f) / N * 2 - 1, r = sqrtf(dx * dx + dy * dy), v;
    if (shape == 1) { float d = fabsf(r - 0.92f) / 0.07f; v = r > 1 ? 0 : fminf(1, fmaxf(0, 1 - d) * 1.6f) + fmaxf(0, r - .55f) * fmaxf(0, 1 - r) * .9f; }
    else { v = fmaxf(0, 1 - r); v = v * v * (0.55f + 2.2f * v * v); v = fminf(1, v); }
    uint8_t b = (uint8_t)(fminf(1, v) * 255);
    uint8_t *q = px + (y * N + x) * 4;
    q[0] = q[1] = q[2] = shape == 2 ? 255 : b; q[3] = shape == 2 ? (uint8_t)(b * 0.55f) : 255;
  }
  t3_texture *t = t3_texture_new(N, N, px);
  t->color_space = T3_LINEAR_SRGB_COLOR_SPACE;
  return t;
}
static t3_material *add_material(t3_texture *t) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  t3_material_set_map(m, t);
  t3_release(t);
  m->transparent = true; m->blending = T3_ADDITIVE_BLENDING; m->depth_write = false; m->side = T3_DOUBLE_SIDE;
  m->fog = false; m->tone_mapped = false; m->version++;
  return m;
}

static t3_scene *FXS;
static float world_floor_y(v3 p) { (void)p; return 0; }
void kfx_attach(t3_scene *sc) {
  if (!glow.p) { kfx_init(sc); return; }
  pool_t *ps[3] = { &glow, &ring, &smoke };
  for (int i = 0; i < 3; i++) {
    t3_object *o = (t3_object *)ps[i]->im;
    t3_retain(o);
    if (o->parent) t3_object_remove(o->parent, o);
    if (sc) t3_object_add(&sc->base, o);
    t3_release(o);
  }
  FXS = sc;
}

void kfx_init(t3_scene *sc) {
  FXS = sc;
  if (glow.p) return;
  glow = pool_make(sc, t3_plane_geometry_new(1, 1, 1, 1), add_material(tex_make(0)), 512);
  ring = pool_make(sc, t3_plane_geometry_new(2, 2, 1, 1), add_material(tex_make(1)), 48);
  t3_material *sm = t3_mesh_basic_material_new(0xffffff);
  t3_texture *st = tex_make(2);
  t3_material_set_map(sm, st); t3_release(st);
  sm->transparent = true; sm->depth_write = false; sm->side = T3_DOUBLE_SIDE; sm->fog = false; sm->version++;
  smoke = pool_make(sc, t3_plane_geometry_new(1, 1, 1, 1), sm, 64);
}

void kfx_clear(void) {
  pool_t *ps[3] = { &glow, &ring, &smoke };
  for (int i = 0; i < 3; i++) { if (ps[i]->p) for (int k = 0; k < ps[i]->cap; k++) ps[i]->p[k].life = 0; ps[i]->im->count = 0; ((t3_object *)ps[i]->im)->visible = false; }
}

static part_t *emit(pool_t *P, int kind, v3 p, v3 v, float life, float s0, float s1, uint32_t hex) {
  part_t *q = &P->p[P->next]; P->next = (P->next + 1) % P->cap;
  memset(q, 0, sizeof *q);
  q->kind = kind; q->p = p; q->v = v; q->life = q->max = life; q->s0 = s0; q->s1 = s1; q->bright = 1;
  t3_color c = t3_color_hex(hex); q->c[0] = c.r; q->c[1] = c.g; q->c[2] = c.b;
  return q;
}
static v3 rnd_dir(void) {
  v3 d; do { d = v3_(ds_rr(-1, 1), ds_rr(-1, 1), ds_rr(-1, 1)); } while (v3_dot(d, d) > 1 || v3_dot(d, d) < 0.01f);
  return v3_norm(d);
}
static void line(v3 a, v3 b, uint32_t col, float w0, float w1, float life) {
  part_t *q = emit(&glow, K_LINE, a, v3_(0, 0, 0), life, 0, 0, col); q->p2 = b; q->w0 = w0; q->w1 = w1;
}

void kfx_beam(v3 from, v3 to, uint32_t color, float width) { line(from, to, color, width, width, 0.001f); }
void kfx_streak(v3 a, v3 b, uint32_t color, float width, float life) { line(a, b, color, width, width * 0.2f, life); }
void kfx_glow(v3 at, float size, uint32_t color) { emit(&glow, K_BILL, at, v3_(0, 0, 0), 0.001f, size, size, color); }
void kfx_ring_flat(v3 at, float radius, uint32_t color) { emit(&ring, K_FLAT, at, v3_(0, 0, 0), 0.001f, radius, radius, color); }
void kfx_flash(v3 at, float size, uint32_t color) { part_t *q = emit(&glow, K_BILL, at, v3_(0, 0, 0), 0.12f, size, size * 0.4f, color); q->rot = ds_rr(0, 6); }
void kfx_shock(v3 at, float r0, float r1, uint32_t color, float life) { emit(&ring, K_FLAT, at, v3_(0, 0, 0), life, r0, r1, color); }
void kfx_ring_up(v3 at, float r0, float r1, uint32_t color, float life) { emit(&ring, K_RINGB, at, v3_(0, 0, 0), life, r0, r1, color); }

void kfx_glint(v3 at, float size, uint32_t color) {
  v3 r = v3_scale(cam_r, size), u = v3_scale(cam_u, size);
  kfx_beam(v3_sub(at, r), v3_add(at, r), color, size * .09f);
  kfx_beam(v3_sub(at, v3_scale(u, .7f)), v3_add(at, v3_scale(u, .7f)), color, size * .09f);
  kfx_glow(at, size * .7f, 0xffffff);
}

void kfx_sparks(v3 at, v3 n, uint32_t color, int count) {
  float fy = world_floor_y(at);
  emit(&glow, K_BILL, at, v3_(0, 0, 0), 0.08f, 0.7f, 0.2f, 0xffffff);
  emit(&glow, K_BILL, at, v3_(0, 0, 0), 0.12f, 1.0f, 0.3f, color);
  for (int i = 0; i < count; i++) {
    v3 v = v3_add(v3_scale(n, ds_rr(2, 7)), v3_scale(rnd_dir(), ds_rr(1, 4.5f)));
    part_t *q = emit(&glow, K_STREAK, at, v, ds_rr(0.2f, 0.5f), 0.07f, 0.02f, i & 1 ? color : 0xfff0c8);
    q->grav = 13; q->w0 = 0.035f; q->fy = fy; q->bounce = 1;
  }
}

void kfx_clash(v3 at, uint32_t color) {
  float fy = world_floor_y(at);
  v3 r = v3_scale(cam_r, .9f), u = v3_scale(cam_u, .55f);
  line(v3_sub(at, r), v3_add(at, r), 0xffffff, .05f, .005f, .12f);
  line(v3_sub(at, u), v3_add(at, u), 0xffffff, .05f, .005f, .12f);
  emit(&glow, K_BILL, at, v3_(0, 0, 0), 0.1f, 1.2f, 0.3f, 0xffffff);
  for (int i = 0; i < 14; i++) {
    part_t *q = emit(&glow, K_STREAK, at, v3_scale(rnd_dir(), ds_rr(4, 10)), ds_rr(0.2f, 0.45f), 0.08f, 0.02f, i & 1 ? color : 0xffffff);
    q->grav = 14; q->w0 = 0.03f; q->fy = fy; q->bounce = 1;
  }
}

void kfx_dust(v3 at, float size, int count) {
  for (int i = 0; i < count; i++) {
    float a = ds_rr(0, 6.28f);
    v3 v = v3_(cosf(a) * ds_rr(.5f, 2.2f) * size, ds_rr(.1f, .8f), sinf(a) * ds_rr(.5f, 2.2f) * size);
    part_t *q = emit(&smoke, K_SMOKE, v3_add(at, v3_(0, .08f, 0)), v, ds_rr(.35f, .7f), size * .35f, size * ds_rr(.9f, 1.4f), 0xb8b0a4);
    q->drag = 3.5f; q->rot = ds_rr(0, 6);
  }
}

void kfx_motes(v3 at, uint32_t color, int count, float speed) {
  for (int i = 0; i < count; i++) {
    v3 d = rnd_dir(); d.y = fabsf(d.y);
    part_t *q = emit(&glow, K_BILL, at, v3_scale(d, ds_rr(.3f, 1) * speed), ds_rr(.4f, .9f), ds_rr(.08f, .2f), .02f, color);
    q->drag = 2.2f; q->grav = -1.2f;
  }
}

void kfx_burst(v3 at, uint32_t color, float size) {
  emit(&glow, K_BILL, at, v3_(0, 0, 0), 0.1f, size * 2.2f, size * .7f, 0xffffff);
  emit(&glow, K_BILL, at, v3_(0, 0, 0), 0.2f, size * 3.f, size * .6f, color);
  emit(&ring, K_RINGB, at, v3_(0, 0, 0), .3f, size * .3f, size * 2.2f, color);
  emit(&ring, K_RINGB, at, v3_(0, 0, 0), .22f, size * .2f, size * 1.4f, 0xffffff);
  int n = 10 + (int)(size * 14);
  for (int i = 0; i < n; i++) {
    v3 d = rnd_dir();
    part_t *q = emit(&glow, K_STREAK, v3_add(at, v3_scale(d, size * .2f)), v3_scale(d, ds_rr(4, 12) * size), ds_rr(.2f, .5f), .1f, .02f, i & 1 ? color : 0xffffff);
    q->w0 = .04f; q->drag = 3;
  }
}

/* write one instance: columns x, y, z axes and translation */
static void put(pool_t *P, int i, v3 x, v3 y, v3 z, v3 p, const float c[3], float k) {
  float *m = (float *)P->im->instance_matrix->array + i * 16;
  m[0] = x.x; m[1] = x.y; m[2] = x.z; m[3] = 0;
  m[4] = y.x; m[5] = y.y; m[6] = y.z; m[7] = 0;
  m[8] = z.x; m[9] = z.y; m[10] = z.z; m[11] = 0;
  m[12] = p.x; m[13] = p.y; m[14] = p.z; m[15] = 1;
  t3_color col = { c[0] * k, c[1] * k, c[2] * k };
  t3_instanced_mesh_set_color_at(P->im, i, col);
}

static void run_pool(pool_t *P, float dt) {
  int n = 0;
  for (int k = 0; k < P->cap; k++) {
    part_t *q = &P->p[k];
    if (q->life <= 0) continue;
    float t = 1 - q->life / q->max, fade = 1 - t;
    float s = ds_lerp(q->s0, q->s1, t);
    v3 view = v3_norm(v3_sub(q->p, cam_p));
    switch (q->kind) {
    case K_SMOKE: {
      float sc = s * fminf(1, fade * 2.5f), cr = cosf(q->rot) * sc, sr = sinf(q->rot) * sc;
      float keep[3] = { q->c[0], q->c[1], q->c[2] };
      put(P, n++, v3_add(v3_scale(cam_r, cr), v3_scale(cam_u, sr)), v3_add(v3_scale(cam_r, -sr), v3_scale(cam_u, cr)), view, q->p, keep, 1);
      break; }
    case K_BILL: case K_RINGB: {
      float cr = cosf(q->rot), sr = sinf(q->rot);
      v3 x = v3_add(v3_scale(cam_r, cr * s), v3_scale(cam_u, sr * s)), y = v3_add(v3_scale(cam_r, -sr * s), v3_scale(cam_u, cr * s));
      put(P, n++, x, y, view, q->p, q->c, fade * fade * q->bright);
      break; }
    case K_FLAT:
      put(P, n++, v3_(s, 0, 0), v3_(0, 0, s), v3_(0, 1, 0), q->p, q->c, fade * q->bright);
      break;
    case K_STREAK: case K_LINE: {
      v3 a, b;
      if (q->kind == K_LINE) { a = q->p; b = q->p2; }
      else { float sp = v3_len(q->v); a = q->p; b = v3_sub(q->p, v3_scale(q->v, (fminf(sp * 0.03f, 1.2f) + s) / fmaxf(sp, 0.01f))); }
      v3 d = v3_sub(b, a); float L = v3_len(d);
      if (L < 1e-4f) { b = v3_add(a, v3_scale(cam_r, 0.01f)); d = v3_sub(b, a); L = 0.01f; }
      v3 dn = v3_scale(d, 1 / L), side = v3_norm(v3_cross(dn, view));
      float w = ds_lerp(q->w0, q->w1 > 0 ? q->w1 : q->w0 * 0.3f, t);
      v3 mid = v3_scale(v3_add(a, b), 0.5f);
      put(P, n++, d, v3_scale(side, w), view, mid, q->c, fade * q->bright);
      break; }
    }
    /* age after drawing, so a one frame effect is drawn once */
    q->life -= dt > 0 ? dt : .002f;
    if (q->kind != K_LINE && q->kind != K_FLAT && q->kind != K_RINGB) {
      q->v.y -= q->grav * dt;
      if (q->drag > 0) { float f = expf(-q->drag * dt); q->v = v3_scale(q->v, f); }
      q->p = v3_add(q->p, v3_scale(q->v, dt));
      if (q->bounce && q->p.y < q->fy + 0.03f && q->v.y < 0) { q->p.y = q->fy + 0.03f; q->v.y *= -0.35f; q->v.x *= 0.6f; q->v.z *= 0.6f; }
    }
  }
  P->im->count = n;
  ((t3_object *)P->im)->visible = n > 0;
  P->im->instance_matrix->version++;
  if (P->im->instance_color) P->im->instance_color->version++;
}

void kfx_update(float dt, const t3_camera *cam) {
  const float *m = cam->base.matrix_world.e;
  cam_r = v3_(m[0], m[1], m[2]); cam_u = v3_(m[4], m[5], m[6]); cam_p = v3_(m[12], m[13], m[14]);
  run_pool(&glow, dt); run_pool(&ring, dt); run_pool(&smoke, dt);
}

void kfx_smoke(v3 at, float size, uint32_t color, int count, float rise) {
  for (int i = 0; i < count; i++) {
    float a = ds_rr(0, 6.28f);
    v3 v = v3_(cosf(a) * ds_rr(.2f, 1.f) * size, ds_rr(.2f, 1.f) * rise, sinf(a) * ds_rr(.2f, 1.f) * size);
    part_t *q = emit(&smoke, K_SMOKE, v3_add(at, v3_(ds_rr(-.3f, .3f) * size, 0, ds_rr(-.3f, .3f) * size)), v, ds_rr(.9f, 1.8f), size * .5f, size * ds_rr(1.4f, 2.2f), color);
    q->drag = 1.2f; q->rot = ds_rr(0, 6);
  }
}

/* ---- props ---- */
typedef struct { float *pos, *nor, *col; int n, cap; } pmb;
static pmb PB[3]; static int cur_layer;
static t3_material *PMAT[3];
void kfxp_begin(void) { for (int i = 0; i < 3; i++) PB[i].n = 0; cur_layer = KFXP_BODY; }
void kfxp_layer(int l) { cur_layer = l < 0 ? 0 : l > 2 ? 2 : l; }
static void ptri(v3 a, v3 b, v3 c, uint32_t ca, uint32_t cb, uint32_t cc) {
  pmb *m = &PB[cur_layer];
  if (m->n + 3 > m->cap) {
    m->cap = m->cap ? m->cap * 2 : 384;
    m->pos = realloc(m->pos, sizeof(float) * 3 * m->cap); m->nor = realloc(m->nor, sizeof(float) * 3 * m->cap); m->col = realloc(m->col, sizeof(float) * 3 * m->cap);
  }
  v3 nn = v3_norm(v3_cross(v3_sub(b, a), v3_sub(c, a)));
  v3 P[3] = {a, b, c}; uint32_t C[3] = {ca, cb, cc};
  for (int i = 0; i < 3; i++) {
    int k = m->n++; t3_color cc_ = t3_color_hex(C[i]);
    m->pos[k*3] = P[i].x; m->pos[k*3+1] = P[i].y; m->pos[k*3+2] = P[i].z;
    m->nor[k*3] = nn.x; m->nor[k*3+1] = nn.y; m->nor[k*3+2] = nn.z;
    m->col[k*3] = cc_.r; m->col[k*3+1] = cc_.g; m->col[k*3+2] = cc_.b;
  }
}
void kfxp_quad(v3 a, v3 b, v3 c, v3 d, uint32_t col) { ptri(a, b, c, col, col, col); ptri(a, c, d, col, col, col); }
void kfxp_box(v3 c, v3 s, uint32_t col) {
  float x0 = c.x - s.x/2, x1 = c.x + s.x/2, y0 = c.y - s.y/2, y1 = c.y + s.y/2, z0 = c.z - s.z/2, z1 = c.z + s.z/2;
  kfxp_quad(v3_(x0,y0,z1), v3_(x1,y0,z1), v3_(x1,y1,z1), v3_(x0,y1,z1), col);
  kfxp_quad(v3_(x1,y0,z0), v3_(x0,y0,z0), v3_(x0,y1,z0), v3_(x1,y1,z0), col);
  kfxp_quad(v3_(x1,y0,z1), v3_(x1,y0,z0), v3_(x1,y1,z0), v3_(x1,y1,z1), col);
  kfxp_quad(v3_(x0,y0,z0), v3_(x0,y0,z1), v3_(x0,y1,z1), v3_(x0,y1,z0), col);
  kfxp_quad(v3_(x0,y1,z1), v3_(x1,y1,z1), v3_(x1,y1,z0), v3_(x0,y1,z0), col);
  kfxp_quad(v3_(x0,y0,z0), v3_(x1,y0,z0), v3_(x1,y0,z1), v3_(x0,y0,z1), col);
}
void kfxp_cyl(v3 b, float h, float r0, float r1, int sides, uint32_t col) {
  for (int k = 0; k < sides; k++) {
    float a0 = k * 6.28318f / sides, a1 = (k + 1) * 6.28318f / sides;
    v3 p00 = v3_(b.x + cosf(a0) * r0, b.y, b.z + sinf(a0) * r0), p01 = v3_(b.x + cosf(a1) * r0, b.y, b.z + sinf(a1) * r0);
    v3 p10 = v3_(b.x + cosf(a0) * r1, b.y + h, b.z + sinf(a0) * r1), p11 = v3_(b.x + cosf(a1) * r1, b.y + h, b.z + sinf(a1) * r1);
    ptri(p00, p11, p10, col, col, col); ptri(p00, p01, p11, col, col, col);
    ptri(v3_(b.x, b.y + h, b.z), p10, p11, col, col, col);
  }
}
void kfxp_disc(v3 c, float r, int sides, uint32_t in, uint32_t out) {
  for (int k = 0; k < sides; k++) {
    float a0 = k * 6.28318f / sides, a1 = (k + 1) * 6.28318f / sides;
    ptri(c, v3_(c.x + cosf(a1) * r, c.y, c.z + sinf(a1) * r), v3_(c.x + cosf(a0) * r, c.y, c.z + sinf(a0) * r), in, out, out);
  }
}
void kfxp_bar(v3 a, v3 b, float w, uint32_t col) {
  v3 d = v3_sub(b, a); float l = v3_len(d); if (l < 1e-4f) return;
  d = v3_scale(d, 1 / l);
  v3 hint = fabsf(d.y) > .9f ? v3_(1, 0, 0) : v3_(0, 1, 0);
  v3 x = v3_scale(v3_norm(v3_cross(d, hint)), w * .5f), y = v3_scale(v3_norm(v3_cross(x, d)), w * .5f);
  v3 c[4] = { v3_add(x, y), v3_sub(x, y), v3_scale(v3_add(x, y), -1), v3_sub(y, x) };
  for (int i = 0; i < 4; i++) { int j = (i + 1) & 3; kfxp_quad(v3_add(a, c[i]), v3_add(a, c[j]), v3_add(b, c[j]), v3_add(b, c[i]), col); }
}
void kfxp_ring(v3 c, v3 ax, float R, float r, int segs, uint32_t col) {
  ax = v3_norm(ax);
  v3 hint = fabsf(ax.y) > .9f ? v3_(1, 0, 0) : v3_(0, 1, 0);
  v3 u = v3_norm(v3_cross(ax, hint)), w = v3_cross(ax, u);
  for (int i = 0; i < segs; i++) {
    float a0 = i * 6.28318f / segs, a1 = (i + 1) * 6.28318f / segs;
    v3 p0 = v3_add(c, v3_add(v3_scale(u, cosf(a0) * R), v3_scale(w, sinf(a0) * R)));
    v3 p1 = v3_add(c, v3_add(v3_scale(u, cosf(a1) * R), v3_scale(w, sinf(a1) * R)));
    kfxp_bar(p0, p1, r * 2, col);
  }
}
t3_object *kfxp_end(void) {
  if (!PMAT[0]) {
    PMAT[0] = t3_mesh_lambert_material_new(0xffffff);
    PMAT[0]->vertex_colors = true; PMAT[0]->fog = true; PMAT[0]->side = T3_DOUBLE_SIDE; PMAT[0]->version++;
    PMAT[1] = t3_mesh_basic_material_new(0xffffff);
    PMAT[1]->vertex_colors = true; PMAT[1]->fog = false; PMAT[1]->side = T3_DOUBLE_SIDE; PMAT[1]->tone_mapped = false; PMAT[1]->version++;
    PMAT[2] = t3_mesh_basic_material_new(0xffffff);
    PMAT[2]->vertex_colors = true; PMAT[2]->fog = false; PMAT[2]->side = T3_DOUBLE_SIDE; PMAT[2]->tone_mapped = false;
    PMAT[2]->transparent = true; PMAT[2]->blending = T3_ADDITIVE_BLENDING; PMAT[2]->depth_write = false; PMAT[2]->version++;
  }
  t3_object *g = t3_group_new();
  for (int i = 0; i < 3; i++) {
    pmb *m = &PB[i];
    if (!m->n) continue;
    t3_geometry *geo = t3_geometry_new();
    t3_attribute *p = t3_attribute_new(T3_FLOAT32, m->pos, m->n, 3), *n = t3_attribute_new(T3_FLOAT32, m->nor, m->n, 3), *c = t3_attribute_new(T3_FLOAT32, m->col, m->n, 3);
    t3_geometry_set_attribute(geo, T3_ATTR_POSITION, p); t3_release(p);
    t3_geometry_set_attribute(geo, T3_ATTR_NORMAL, n); t3_release(n);
    t3_geometry_set_attribute(geo, T3_ATTR_COLOR, c); t3_release(c);
    t3_mesh *me = t3_mesh_new(geo, PMAT[i]); t3_release(geo);
    ((t3_object *)me)->frustum_culled = false;
    if (i == 2) ((t3_object *)me)->render_order = 3;
    t3_object_add(g, me); t3_release(me);
    m->n = 0;
  }
  t3_object_add(&FXS->base, g);
  t3_release(g);
  return g;
}

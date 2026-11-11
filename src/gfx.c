/* gfx.c: see gfx.h. The page's $r, Av, Pv (with X0's star texture) and ev. */
#include "gfx.h"
#include "fx.h"
#include "cv.h"
#include "ck.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- $r: the particles (ShaderMaterial, additive, depthWrite false) ---- */
#define PMAX 1100
static int pn;
static float ppos[PMAX * 3], pvel[PMAX * 3], pbase[PMAX * 4], plife[PMAX], plife0[PMAX], pgrav[PMAX], pdrag[PMAX], psz0[PMAX];
static float pcol[PMAX * 4], psize[PMAX];
static float uscale = 600;

void gfx_set_scale(float h, float fov) { uscale = h / (2 * tanf(fov * CK_PI / 360)); }

burst_opts burst_defaults(void) {
  burst_opts o = {5, false, {0, 0, 0}, 1, 0.6f, 0.06f, {1, 0.7f, 0.2f}, 9, 0.8f, 0.5f};
  return o;
}

static void emit(gv3 A, gv3 P, float life, float size, const float *col, float a, float grav, float drag) {
  if (pn >= PMAX) return;
  int n = pn++;
  ppos[n * 3] = A.x; ppos[n * 3 + 1] = A.y; ppos[n * 3 + 2] = A.z;
  pvel[n * 3] = P.x; pvel[n * 3 + 1] = P.y; pvel[n * 3 + 2] = P.z;
  pbase[n * 4] = col[0]; pbase[n * 4 + 1] = col[1]; pbase[n * 4 + 2] = col[2]; pbase[n * 4 + 3] = a;
  plife[n] = plife0[n] = life; psz0[n] = size; pgrav[n] = grav; pdrag[n] = drag;
}

void parts_burst(gv3 A, int P, const burst_opts *o) {
  for (int k = 0; k < P; k++) {
    gv3 B = {ck_rand() - 0.5f, ck_rand() - 0.2f, ck_rand() - 0.5f};
    float l = sqrtf(B.x * B.x + B.y * B.y + B.z * B.z); if (l > 0) { B.x /= l; B.y /= l; B.z /= l; }
    float m = o->speed * (0.4f + ck_rand() * 0.8f);
    B.x *= m; B.y *= m; B.z *= m;
    if (o->has_dir) {
      float s = o->speed * (0.5f + ck_rand() * 0.8f);
      B.x = B.x * o->spread + o->dir.x * s; B.y = B.y * o->spread + o->dir.y * s; B.z = B.z * o->spread + o->dir.z * s;
    }
    float z[3];
    for (int c = 0; c < 3; c++) z[c] = fminf(1, o->color[c] * (1 - o->jitter * 0.4f + ck_rand() * o->jitter * 0.6f));
    float life = o->life * (0.5f + ck_rand() * 0.7f), size = o->size * (0.6f + ck_rand() * 0.8f);
    emit(A, B, life, size, z, 1, o->gravity, o->drag);
  }
}

void parts_update(float A) {
  int P = 0;
  while (P < pn) {
    plife[P] -= A;
    if (plife[P] <= 0) {
      int D = pn - 1;
      if (P != D) {
        memcpy(ppos + P * 3, ppos + D * 3, 12); memcpy(pvel + P * 3, pvel + D * 3, 12); memcpy(pbase + P * 4, pbase + D * 4, 16);
        plife[P] = plife[D]; plife0[P] = plife0[D]; psz0[P] = psz0[D]; pgrav[P] = pgrav[D]; pdrag[P] = pdrag[D];
      }
      pn--;
      continue;
    }
    float e = fmaxf(0, 1 - pdrag[P] * A);
    pvel[P * 3] *= e; pvel[P * 3 + 1] = pvel[P * 3 + 1] * e - pgrav[P] * A; pvel[P * 3 + 2] *= e;
    ppos[P * 3] += pvel[P * 3] * A; ppos[P * 3 + 1] += pvel[P * 3 + 1] * A; ppos[P * 3 + 2] += pvel[P * 3 + 2] * A;
    if (ppos[P * 3 + 1] < 0.02f) { ppos[P * 3 + 1] = 0.02f; pvel[P * 3 + 1] *= -0.35f; pvel[P * 3] *= 0.6f; pvel[P * 3 + 2] *= 0.6f; }
    float t = plife[P] / plife0[P];
    pcol[P * 4] = pbase[P * 4]; pcol[P * 4 + 1] = pbase[P * 4 + 1]; pcol[P * 4 + 2] = pbase[P * 4 + 2];
    pcol[P * 4 + 3] = pbase[P * 4 + 3] * fminf(1, t * 2);
    psize[P] = psz0[P] * (0.4f + 0.6f * t);
    P++;
  }
}

/* ---- Av: the weapon trail (a ribbon of the last n blade segments) ---- */
struct trail { int n; gv3 pts[32][2]; int np; float col[3]; };
trail *trail_new(uint32_t color, int n) {
  trail *t = calloc(1, sizeof *t);
  t->n = n > 32 ? 32 : n;
  trail_set_color(t, color);
  return t;
}
void trail_set_color(trail *t, uint32_t c) { fx_hex_linear(c, t->col); }   /* new YA(P): a Color, stored linear */
void trail_push(trail *t, gv3 a, gv3 b) {
  /* unshift, then pop past n */
  int keep = t->np < t->n ? t->np : t->n - 1;
  memmove(&t->pts[1], &t->pts[0], sizeof t->pts[0] * keep);
  t->pts[0][0] = a; t->pts[0][1] = b;
  t->np = keep + 1;
}
void trail_decay(trail *t) { if (t->np) t->np--; }
void trail_clear(trail *t) { t->np = 0; }
void trail_update(trail *t) { (void)t; }

static void trail_draw(trail *t) {
  int A = t->n, P = t->np;
  if (P <= 1) return;   /* mesh.visible = P > 1 */
  float pos[32 * 2 * 3], alpha[32 * 2];
  for (int e = 0; e < A; e++) {
    int k = e < P - 1 ? e : P - 1;
    gv3 *q = t->pts[k];
    pos[e * 6] = q[0].x; pos[e * 6 + 1] = q[0].y; pos[e * 6 + 2] = q[0].z;
    pos[e * 6 + 3] = q[1].x; pos[e * 6 + 4] = q[1].y; pos[e * 6 + 5] = q[1].z;
    float D = e < P ? 1 - (float)e / fmaxf(1, P - 1) : 0;
    alpha[e * 2] = alpha[e * 2 + 1] = D * D;
  }
  /* index g, g+1, g+2, g+1, g+3, g+2 per quad; the shader writes uColor * 1.3, alpha vA * .75 */
  float xyz[31 * 6 * 3], rgba[31 * 6 * 4];
  int nv = 0;
  for (int w = 0; w < A - 1; w++) {
    int g = w * 2, idx[6] = {g, g + 1, g + 2, g + 1, g + 3, g + 2};
    for (int k = 0; k < 6; k++) {
      int i = idx[k];
      memcpy(xyz + nv * 3, pos + i * 3, 12);
      rgba[nv * 4] = t->col[0] * 1.3f; rgba[nv * 4 + 1] = t->col[1] * 1.3f; rgba[nv * 4 + 2] = t->col[2] * 1.3f;
      rgba[nv * 4 + 3] = alpha[i] * 0.75f;
      nv++;
    }
  }
  static const float one[4] = {1, 1, 1, 1};
  fx_tris(xyz, NULL, rgba, nv, NULL, 0, one, FXB_ADD, FXF_DEPTH_TEST | FXF_TEX_LINEAR, true);
}

/* ---- Pv: the impact stars (Sprites with X0's texture) ---- */
static unsigned tex_pink, tex_blue;
static unsigned star_tex(const char *s, const char *A) {
  cv *c = cv_new(256, 256);
  cv_translate(c, 128, 128);
  const int D = 13;
  const char *fills[3] = {s, A, "#fff"};
  const float in[3] = {70, 46, 24}, out[3] = {124, 90, 50};
  for (int L = 0; L < 3; L++) {
    cv_begin(c);
    for (int v = 0; v < D * 2; v++) {
      float o = v / (float)(D * 2) * CK_PI * 2 + 0.2f, B = ((v % 2) ? in[L] : out[L]) * (1 + 0.12f * sinf(v * 7.3f));
      cv_line(c, cosf(o) * B, sinf(o) * B);
    }
    cv_close(c); cv_fill_css(c, fills[L]); cv_fill(c);
  }
  uint8_t *px = malloc(256 * 256 * 4);
  cv_read(c, px);
  unsigned t = fx_texture(px, 256, 256, false, true);
  free(px); cv_free(c);
  return t;
}
typedef struct { gv3 p; float t, dur, sc, rot; bool blue; } star;
static star stars[64]; static int nstars;
void bursts_spawn(gv3 A, float P, bool blocked) {
  if (nstars >= 64) return;
  stars[nstars++] = (star){A, 0, 0.28f, P, ck_rand() * 6.28f, blocked};
}
void bursts_update(float A) {
  for (int P = nstars - 1; P >= 0; P--) {
    stars[P].t += A;
    if (stars[P].t / stars[P].dur >= 1) { stars[P] = stars[--nstars]; }
  }
}

/* ---- ev: the shock rings (RingGeometry(.85, 1, 64), additive) ---- */
typedef struct { gv3 p; float t, dur, max; float col[3]; } ring;
static ring rings[16]; static int nrings;
static float ring_xyz[64 * 6 * 3];
void rings_spawn(gv3 A, float max, uint32_t color, float dur) {
  if (nrings >= 16) return;
  ring r = {{A.x, 0.05f, A.z}, 0, dur, max, {0, 0, 0}};
  fx_hex_linear(color, r.col);
  rings[nrings++] = r;
}
void rings_update(float A) {
  for (int P = nrings - 1; P >= 0; P--) {
    rings[P].t += A;
    if (rings[P].t / rings[P].dur >= 1) rings[P] = rings[--nrings];
  }
}

void gfx_init(void) {
  tex_pink = star_tex("#f2ab1e", "#f2ab1e");
  tex_blue = star_tex("#5e93c4", "#ffffff");
  /* the ring, flat on the floor (rotation.x = -PI / 2: y up becomes -z) */
  int k = 0;
  for (int i = 0; i < 64; i++) {
    float a0 = i / 64.f * CK_PI * 2, a1 = (i + 1) / 64.f * CK_PI * 2;
    float p[4][3] = {{cosf(a0) * 0.85f, 0, -sinf(a0) * 0.85f}, {cosf(a0), 0, -sinf(a0)}, {cosf(a1), 0, -sinf(a1)}, {cosf(a1) * 0.85f, 0, -sinf(a1) * 0.85f}};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int j = 0; j < 6; j++) { memcpy(ring_xyz + k * 3, p[idx[j]], 12); k++; }
  }
}

void gfx_clear(void) { pn = 0; nstars = 0; nrings = 0; }

void gfx_draw(trail **trails, int ntrails) {
  /* rings: renderOrder 8, MeshBasicMaterial (toneMapped, fog), additive, double sided */
  for (int i = 0; i < nrings; i++) {
    ring *r = &rings[i];
    float t = r->t / r->dur, D = 1 - powf(1 - t, 2.2f), s = 0.2f + r->max * D;
    float m[16] = {s, 0, 0, 0, 0, s, 0, 0, 0, 0, s, 0, r->p.x, r->p.y, r->p.z, 1};
    float col[4] = {r->col[0], r->col[1], r->col[2], (1 - t) * 0.9f};
    fx_tris(ring_xyz, NULL, NULL, 64 * 6, m, 0, col, FXB_ADD, FXF_FOG | FXF_TONEMAP | FXF_ENCODE | FXF_DEPTH_TEST, true);
  }
  for (int i = 0; i < ntrails; i++) if (trails[i]) trail_draw(trails[i]);
  /* particles: renderOrder 10, the page's point shader */
  if (pn > 0) {
    static const float one[4] = {1, 1, 1, 1};
    fx_points(ppos, pcol, psize, pn, 0, uscale, FXP_SOFT_DISC, 0, one, FXB_ADD, FXF_DEPTH_TEST);
  }
  /* stars: renderOrder 20, SpriteMaterial (toneMapped, fog), depthTest false */
  for (int i = 0; i < nstars; i++) {
    star *s = &stars[i];
    float t = s->t / s->dur, D = 1 - powf(1 - t, 3);
    float col[4] = {1, 1, 1, 1 - t * t};
    float p[3] = {s->p.x, s->p.y, s->p.z};
    fx_sprite(p, (0.25f + 1.1f * D) * s->sc, s->rot, s->blue ? tex_blue : tex_pink, col, FXB_NORMAL, FXF_FOG | FXF_TONEMAP | FXF_ENCODE);
  }
}

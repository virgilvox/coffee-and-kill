/* geom.c: see geom.h. TubeGeometry, Curve.computeFrenetFrames,
 * ExtrudeGeometry and earcut (the small-polygon path, no holes) of three.js
 * r186, in the order three.js emits vertices. */
#include "geom.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* ---- TubeGeometry ---- */

static t3_vec3 tangent_at(t3_curve *c, float u) {
  /* Curve.getTangentAt -> getTangent(getUtoTmapping(u)): central difference, delta 1e-4 */
  float t = t3_curve_u_to_t(c, u);
  float t1 = t - 0.0001f, t2 = t + 0.0001f;
  if (t1 < 0) t1 = 0;
  if (t2 > 1) t2 = 1;
  t3_vec3 a = t3_curve_get_point(c, t1), b = t3_curve_get_point(c, t2);
  return t3_vec3_normalize(t3_vec3_sub(b, a));
}

static t3_vec3 rotate_axis(t3_vec3 v, t3_vec3 axis, float theta) {
  /* v.applyMatrix4(makeRotationAxis(axis, theta)) */
  float c = cosf(theta), s = sinf(theta), t = 1 - c;
  float x = axis.x, y = axis.y, z = axis.z, tx = t * x, ty = t * y;
  float m00 = tx * x + c, m01 = tx * y - s * z, m02 = tx * z + s * y;
  float m10 = tx * y + s * z, m11 = ty * y + c, m12 = ty * z - s * x;
  float m20 = tx * z - s * y, m21 = ty * z + s * x, m22 = t * z * z + c;
  return t3_v3(m00 * v.x + m01 * v.y + m02 * v.z, m10 * v.x + m11 * v.y + m12 * v.z, m20 * v.x + m21 * v.y + m22 * v.z);
}

t3_geometry *geom_tube(const t3_vec3 *pts, int n, bool closed_curve, int centripetal, float tension,
                       int P, float radius, int R, bool closed) {
  t3_curve *c = t3_catmull_rom_curve3_new(pts, n, closed_curve, centripetal ? T3_CENTRIPETAL : T3_CATMULLROM, tension);
  /* computeFrenetFrames(P, closed) */
  t3_vec3 *T = malloc(sizeof(t3_vec3) * (P + 1)), *N = malloc(sizeof(t3_vec3) * (P + 1)), *B = malloc(sizeof(t3_vec3) * (P + 1));
  for (int i = 0; i <= P; i++) T[i] = tangent_at(c, (float)i / P);
  float mn = FLT_MAX, tx = fabsf(T[0].x), ty = fabsf(T[0].y), tz = fabsf(T[0].z);
  t3_vec3 normal = t3_v3(0, 0, 0);
  if (tx <= mn) { mn = tx; normal = t3_v3(1, 0, 0); }
  if (ty <= mn) { mn = ty; normal = t3_v3(0, 1, 0); }
  if (tz <= mn) normal = t3_v3(0, 0, 1);
  t3_vec3 vec = t3_vec3_normalize(t3_vec3_cross(T[0], normal));
  N[0] = t3_vec3_cross(T[0], vec);
  B[0] = t3_vec3_cross(T[0], N[0]);
  for (int i = 1; i <= P; i++) {
    N[i] = N[i - 1]; B[i] = B[i - 1];
    vec = t3_vec3_cross(T[i - 1], T[i]);
    if (t3_vec3_length(vec) > FLT_EPSILON) {
      vec = t3_vec3_normalize(vec);
      float d = t3_vec3_dot(T[i - 1], T[i]);
      d = d < -1 ? -1 : d > 1 ? 1 : d;
      N[i] = rotate_axis(N[i], vec, acosf(d));
    }
    B[i] = t3_vec3_cross(T[i], N[i]);
  }
  if (closed) {
    float d = t3_vec3_dot(N[0], N[P]); d = d < -1 ? -1 : d > 1 ? 1 : d;
    float theta = acosf(d) / P;
    if (t3_vec3_dot(T[0], t3_vec3_cross(N[0], N[P])) > 0) theta = -theta;
    for (int i = 1; i <= P; i++) { N[i] = rotate_axis(N[i], T[i], theta * i); B[i] = t3_vec3_cross(T[i], N[i]); }
  }
  int nv = (P + 1) * (R + 1);
  float *pos = malloc(sizeof(float) * nv * 3), *nor = malloc(sizeof(float) * nv * 3), *uv = malloc(sizeof(float) * nv * 2);
  int k = 0;
  for (int j = 0; j <= P; j++) {
    int seg = j < P ? j : (closed ? 0 : P);
    t3_vec3 r = t3_curve_get_point_at(c, (float)seg / P);
    t3_vec3 f = N[seg], u = B[seg];
    for (int t = 0; t <= R; t++) {
      float E = (float)t / R * 6.2831853f, I = sinf(E), h = -cosf(E);
      t3_vec3 nn = t3_vec3_normalize(t3_v3(h * f.x + I * u.x, h * f.y + I * u.y, h * f.z + I * u.z));
      nor[k * 3] = nn.x; nor[k * 3 + 1] = nn.y; nor[k * 3 + 2] = nn.z;
      pos[k * 3] = r.x + radius * nn.x; pos[k * 3 + 1] = r.y + radius * nn.y; pos[k * 3 + 2] = r.z + radius * nn.z;
      k++;
    }
  }
  k = 0;
  for (int j = 0; j <= P; j++) for (int f = 0; f <= R; f++) { uv[k * 2] = (float)j / P; uv[k * 2 + 1] = (float)f / R; k++; }
  int ni = P * R * 6;
  uint32_t *idx = malloc(sizeof(uint32_t) * ni);
  k = 0;
  for (int j = 1; j <= P; j++) for (int f = 1; f <= R; f++) {
    uint32_t a = (R + 1) * (j - 1) + (f - 1), b = (R + 1) * j + (f - 1), cc = (R + 1) * j + f, d = (R + 1) * (j - 1) + f;
    idx[k++] = a; idx[k++] = b; idx[k++] = d;
    idx[k++] = b; idx[k++] = cc; idx[k++] = d;
  }
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nor, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, nv, 2));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT32, idx, ni, 1));
  free(pos); free(nor); free(uv); free(idx); free(T); free(N); free(B);
  t3_release(c);
  return g;
}

/* ---- earcut (no holes; under 80 points, no z-order hashing) ---- */

typedef struct node { int i; float x, y; struct node *prev, *next; bool steiner; } node;
typedef struct { node *pool; int n, cap; int *tri; int nt, tcap; } ectx;

static node *mk(ectx *e, int i, float x, float y) {
  node *p = &e->pool[e->n++];
  p->i = i; p->x = x; p->y = y; p->prev = p->next = NULL; p->steiner = false;
  return p;
}
static node *insert_node(ectx *e, int i, float x, float y, node *last) {
  node *p = mk(e, i, x, y);
  if (!last) { p->prev = p; p->next = p; }
  else { p->next = last->next; p->prev = last; last->next->prev = p; last->next = p; }
  return p;
}
static void remove_node(node *p) { p->next->prev = p->prev; p->prev->next = p->next; }
static bool equals(const node *a, const node *b) { return a->x == b->x && a->y == b->y; }
static float area(const node *p, const node *q, const node *r) { return (q->y - p->y) * (r->x - q->x) - (q->x - p->x) * (r->y - q->y); }
static bool pit(float ax, float ay, float bx, float by, float cx, float cy, float px, float py) {
  return (cx - px) * (ay - py) >= (ax - px) * (cy - py) && (ax - px) * (by - py) >= (bx - px) * (ay - py) &&
         (bx - px) * (cy - py) >= (cx - px) * (by - py);
}
static bool pit_ex_first(float ax, float ay, float bx, float by, float cx, float cy, float px, float py) {
  return !(ax == px && ay == py) && pit(ax, ay, bx, by, cx, cy, px, py);
}
static int sgn(float v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static bool on_segment(const node *p, const node *q, const node *r) {
  return q->x <= fmaxf(p->x, r->x) && q->x >= fminf(p->x, r->x) && q->y <= fmaxf(p->y, r->y) && q->y >= fminf(p->y, r->y);
}
static bool intersects(const node *p1, const node *q1, const node *p2, const node *q2) {
  int o1 = sgn(area(p1, q1, p2)), o2 = sgn(area(p1, q1, q2)), o3 = sgn(area(p2, q2, p1)), o4 = sgn(area(p2, q2, q1));
  if (o1 != o2 && o3 != o4) return true;
  if (o1 == 0 && on_segment(p1, p2, q1)) return true;
  if (o2 == 0 && on_segment(p1, q2, q1)) return true;
  if (o3 == 0 && on_segment(p2, p1, q2)) return true;
  if (o4 == 0 && on_segment(p2, q1, q2)) return true;
  return false;
}
static bool locally_inside(const node *a, const node *b) {
  return area(a->prev, a, a->next) < 0 ? area(a, b, a->next) >= 0 && area(a, a->prev, b) >= 0
                                        : area(a, b, a->prev) < 0 || area(a, a->next, b) < 0;
}
static bool intersects_polygon(const node *a, const node *b) {
  const node *p = a;
  do {
    if (p->i != a->i && p->next->i != a->i && p->i != b->i && p->next->i != b->i && intersects(p, p->next, a, b)) return true;
    p = p->next;
  } while (p != a);
  return false;
}
static bool middle_inside(const node *a, const node *b) {
  const node *p = a; bool inside = false; float px = (a->x + b->x) / 2, py = (a->y + b->y) / 2;
  do {
    if (((p->y > py) != (p->next->y > py)) && p->next->y != p->y && (px < (p->next->x - p->x) * (py - p->y) / (p->next->y - p->y) + p->x))
      inside = !inside;
    p = p->next;
  } while (p != a);
  return inside;
}
static bool valid_diagonal(const node *a, const node *b) {
  return a->next->i != b->i && a->prev->i != b->i && !intersects_polygon(a, b) &&
         ((locally_inside(a, b) && locally_inside(b, a) && middle_inside(a, b) && (area(a->prev, a, b->prev) != 0 || area(a, b->prev, b) != 0)) ||
          (equals(a, b) && area(a->prev, a, a->next) > 0 && area(b->prev, b, b->next) > 0));
}
static node *split_polygon(ectx *e, node *a, node *b) {
  node *a2 = mk(e, a->i, a->x, a->y), *b2 = mk(e, b->i, b->x, b->y), *an = a->next, *bp = b->prev;
  a->next = b; b->prev = a;
  a2->next = an; an->prev = a2;
  b2->next = a2; a2->prev = b2;
  bp->next = b2; b2->prev = bp;
  return b2;
}
static node *filter_points(node *start, node *end) {
  if (!start) return start;
  if (!end) end = start;
  node *p = start; bool again;
  do {
    again = false;
    if (!p->steiner && (equals(p, p->next) || area(p->prev, p, p->next) == 0)) {
      remove_node(p);
      p = end = p->prev;
      if (p == p->next) break;
      again = true;
    } else p = p->next;
  } while (again || p != end);
  return end;
}
static void push_tri(ectx *e, int a, int b, int c) {
  if (e->nt + 3 > e->tcap) { e->tcap = e->tcap * 2 + 48; e->tri = realloc(e->tri, sizeof(int) * e->tcap); }
  e->tri[e->nt++] = a; e->tri[e->nt++] = b; e->tri[e->nt++] = c;
}
static bool is_ear(const node *ear) {
  const node *a = ear->prev, *b = ear, *c = ear->next;
  if (area(a, b, c) >= 0) return false;
  float ax = a->x, bx = b->x, cx = c->x, ay = a->y, by = b->y, cy = c->y;
  float x0 = fminf(ax, fminf(bx, cx)), y0 = fminf(ay, fminf(by, cy)), x1 = fmaxf(ax, fmaxf(bx, cx)), y1 = fmaxf(ay, fmaxf(by, cy));
  const node *p = c->next;
  while (p != a) {
    if (p->x >= x0 && p->x <= x1 && p->y >= y0 && p->y <= y1 && pit_ex_first(ax, ay, bx, by, cx, cy, p->x, p->y) &&
        area(p->prev, p, p->next) >= 0) return false;
    p = p->next;
  }
  return true;
}
static void earcut_linked(ectx *e, node *ear, int pass);
static node *cure_local(ectx *e, node *start) {
  node *p = start;
  do {
    node *a = p->prev, *b = p->next->next;
    if (!equals(a, b) && intersects(a, p, p->next, b) && locally_inside(a, b) && locally_inside(b, a)) {
      push_tri(e, a->i, p->i, b->i);
      remove_node(p); remove_node(p->next);
      p = start = b;
    }
    p = p->next;
  } while (p != start);
  return filter_points(p, NULL);
}
static void split_earcut(ectx *e, node *start) {
  node *a = start;
  do {
    node *b = a->next->next;
    while (b != a->prev) {
      if (a->i != b->i && valid_diagonal(a, b)) {
        node *c = split_polygon(e, a, b);
        a = filter_points(a, a->next); c = filter_points(c, c->next);
        earcut_linked(e, a, 0); earcut_linked(e, c, 0);
        return;
      }
      b = b->next;
    }
    a = a->next;
  } while (a != start);
}
static void earcut_linked(ectx *e, node *ear, int pass) {
  if (!ear) return;
  node *stop = ear;
  while (ear->prev != ear->next) {
    node *prev = ear->prev, *next = ear->next;
    if (is_ear(ear)) {
      push_tri(e, prev->i, ear->i, next->i);
      remove_node(ear);
      ear = next->next; stop = next->next;
      continue;
    }
    ear = next;
    if (ear == stop) {
      if (!pass) earcut_linked(e, filter_points(ear, NULL), 1);
      else if (pass == 1) earcut_linked(e, cure_local(e, filter_points(ear, NULL)), 2);
      else split_earcut(e, ear);
      break;
    }
  }
}
/* earcut(data) of n points: returns the triangle index count in *nt */
static int *earcut(const float *d, int n, int *nt) {
  ectx e = {0};
  e.cap = n * 4 + 16; e.pool = calloc(e.cap, sizeof(node));
  float sum = 0;   /* signedArea */
  for (int i = 0, j = n - 1; i < n; j = i++) sum += (d[j * 2] - d[i * 2]) * (d[i * 2 + 1] + d[j * 2 + 1]);
  node *last = NULL;
  if (sum > 0) for (int i = 0; i < n; i++) last = insert_node(&e, i, d[i * 2], d[i * 2 + 1], last);
  else for (int i = n - 1; i >= 0; i--) last = insert_node(&e, i, d[i * 2], d[i * 2 + 1], last);
  if (last && equals(last, last->next)) { remove_node(last); last = last->next; }
  if (last && last->next != last->prev) earcut_linked(&e, last, 0);
  free(e.pool);
  *nt = e.nt;
  return e.tri;
}

/* ---- ExtrudeGeometry ---- */

typedef struct { float x, y; } v2;
typedef struct { float *a; int n, cap; } fbuf;
static void fpush(fbuf *b, float v) { if (b->n == b->cap) { b->cap = b->cap * 2 + 64; b->a = realloc(b->a, sizeof(float) * b->cap); } b->a[b->n++] = v; }

static v2 bevel_vec(v2 w, v2 v, v2 c) {
  /* getBevelVec(inPt, inPrev, inNext) */
  float zA, X, PA;
  float tA = w.x - v.x, iA = w.y - v.y, cA = c.x - w.x, U = c.y - w.y, HA = tA * tA + iA * iA, KA = tA * U - iA * cA;
  if (fabsf(KA) > FLT_EPSILON) {
    float L = sqrtf(HA), M = sqrtf(cA * cA + U * U);
    float F = v.x - iA / L, Z = v.y + tA / L, q = c.x - U / M, oA = c.y + cA / M;
    float BA = ((q - F) * U - (oA - Z) * cA) / (tA * U - iA * cA);
    zA = F + tA * BA - w.x; X = Z + iA * BA - w.y;
    float eA = zA * zA + X * X;
    if (eA <= 2) return (v2){zA, X};
    PA = sqrtf(eA / 2);
  } else {
    bool L = false;
    if (tA > FLT_EPSILON) { if (cA > FLT_EPSILON) L = true; }
    else if (tA < -FLT_EPSILON) { if (cA < -FLT_EPSILON) L = true; }
    else if (sgn(iA) == sgn(U)) L = true;
    if (L) { zA = -iA; X = tA; PA = sqrtf(HA); } else { zA = tA; X = iA; PA = sqrtf(HA / 2); }
  }
  return (v2){zA / PA, X / PA};
}

t3_geometry *geom_extrude(const float *xy, int n, float depth, bool bevel, float B, float z, int c) {
  /* Shape.extractPoints: the polygon closed by repeating its first point; the
   * duplicate filter then drops the first copy, so the run starts at point 1 */
  v2 *d = malloc(sizeof(v2) * (n + 1));
  int G = 0;
  for (int i = 1; i < n; i++) d[G++] = (v2){xy[i * 2], xy[i * 2 + 1]};
  d[G++] = (v2){xy[0], xy[1]};
  float ar = 0;
  for (int t = G - 1, D = 0; D < G; t = D++) ar += d[t].x * d[D].y - d[D].x * d[t].y;
  if (!(ar * 0.5f < 0)) for (int i = 0; i < G / 2; i++) { v2 tmp = d[i]; d[i] = d[G - 1 - i]; d[G - 1 - i] = tmp; }
  if (!bevel) { c = 0; B = 0; z = 0; }
  int r = 1;   /* steps */
  float a = 0; /* bevelOffset */
  v2 *S = malloc(sizeof(v2) * G);
  for (int i = 0, j = G - 1, k = 1; i < G; i++, j++, k++) { if (j == G) j = 0; if (k == G) k = 0; S[i] = bevel_vec(d[i], d[j], d[k]); }
  fbuf pts = {0};
  int nt; int *tri;
  float *cap = malloc(sizeof(float) * G * 2);
  if (c == 0) {
    for (int i = 0; i < G; i++) { cap[i * 2] = d[i].x; cap[i * 2 + 1] = d[i].y; }
    tri = earcut(cap, G, &nt);
  } else {
    for (int CA = 0; CA < c; CA++) {
      float zA = (float)CA / c, X = B * cosf(zA * 3.14159265f / 2), PA = z * sinf(zA * 3.14159265f / 2) + a;
      for (int t = 0; t < G; t++) {
        float x = d[t].x + S[t].x * PA, y = d[t].y + S[t].y * PA;
        fpush(&pts, x); fpush(&pts, y); fpush(&pts, -X);
        if (CA == 0) { cap[t * 2] = x; cap[t * 2 + 1] = y; }
      }
    }
    tri = earcut(cap, G, &nt);
  }
  float AP = z + a;
  for (int i = 0; i < G; i++) { fpush(&pts, bevel ? d[i].x + S[i].x * AP : d[i].x); fpush(&pts, bevel ? d[i].y + S[i].y * AP : d[i].y); fpush(&pts, 0); }
  for (int s = 1; s <= r; s++) for (int i = 0; i < G; i++) {
    fpush(&pts, bevel ? d[i].x + S[i].x * AP : d[i].x); fpush(&pts, bevel ? d[i].y + S[i].y * AP : d[i].y); fpush(&pts, depth / r * s);
  }
  for (int w = c - 1; w >= 0; w--) {
    float vA = (float)w / c, CA = B * cosf(vA * 3.14159265f / 2), zA = z * sinf(vA * 3.14159265f / 2) + a;
    for (int t = 0; t < G; t++) { fpush(&pts, d[t].x + S[t].x * zA); fpush(&pts, d[t].y + S[t].y * zA); fpush(&pts, depth + CA); }
  }
  fbuf pos = {0}, uv = {0};
  const float *P3 = pts.a;
#define V(k) do { fpush(&pos, P3[(k) * 3]); fpush(&pos, P3[(k) * 3 + 1]); fpush(&pos, P3[(k) * 3 + 2]); } while (0)
  /* buildLidFaces: generateTopUV is the x, y of each corner */
  int start = 0;
  int layer_top = bevel ? (r + c * 2) : r;
  for (int pass = 0; pass < 2; pass++) {
    for (int k = 0; k < nt; k += 3) {
      int i0, i1, i2;
      if (pass == 0) { i0 = tri[k + 2]; i1 = tri[k + 1]; i2 = tri[k]; }
      else { int o = G * layer_top; i0 = tri[k] + o; i1 = tri[k + 1] + o; i2 = tri[k + 2] + o; }
      V(i0); V(i1); V(i2);
      fpush(&uv, P3[i0 * 3]); fpush(&uv, P3[i0 * 3 + 1]);
      fpush(&uv, P3[i1 * 3]); fpush(&uv, P3[i1 * 3 + 1]);
      fpush(&uv, P3[i2 * 3]); fpush(&uv, P3[i2 * 3 + 1]);
    }
  }
  int lid_count = pos.n / 3 - start;
  /* buildSideFaces: sidewalls(contour, 0) */
  int side_start = pos.n / 3;
  for (int CA = G; --CA >= 0;) {
    int zA = CA, X = CA - 1;
    if (X < 0) X = G - 1;
    for (int L = 0, tA = r + c * 2; L < tA; L++) {
      int iA = G * L, cA = G * (L + 1);
      int A0 = zA + iA, B0 = X + iA, C0 = X + cA, D0 = zA + cA;
      V(A0); V(B0); V(D0); V(B0); V(C0); V(D0);
      /* generateSideWallUV(a, b, c, d) over the four corners */
      const float *pa = P3 + A0 * 3, *pb = P3 + B0 * 3, *pc = P3 + C0 * 3, *pd = P3 + D0 * 3;
      float u[4][2];
      if (fabsf(pa[1] - pb[1]) < fabsf(pa[0] - pb[0])) {
        u[0][0] = pa[0]; u[0][1] = 1 - pa[2]; u[1][0] = pb[0]; u[1][1] = 1 - pb[2];
        u[2][0] = pc[0]; u[2][1] = 1 - pc[2]; u[3][0] = pd[0]; u[3][1] = 1 - pd[2];
      } else {
        u[0][0] = pa[1]; u[0][1] = 1 - pa[2]; u[1][0] = pb[1]; u[1][1] = 1 - pb[2];
        u[2][0] = pc[1]; u[2][1] = 1 - pc[2]; u[3][0] = pd[1]; u[3][1] = 1 - pd[2];
      }
      static const int ord[6] = {0, 1, 3, 1, 2, 3};
      for (int q = 0; q < 6; q++) { fpush(&uv, u[ord[q]][0]); fpush(&uv, u[ord[q]][1]); }
    }
  }
#undef V
  int nv = pos.n / 3;
  t3_geometry *g = t3_geometry_new();
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos.a, nv, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv.a, nv, 2));
  t3_geometry_add_group(g, start, lid_count, 0);
  t3_geometry_add_group(g, side_start, nv - side_start, 1);
  t3_geometry_compute_vertex_normals(g);
  free(pos.a); free(uv.a); free(pts.a); free(d); free(S); free(cap); free(tri);
  return g;
}

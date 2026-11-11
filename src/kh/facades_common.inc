/* facades_common.inc: the small builder the facades use, after bikemesa's view.c and whimsy.inc
 * (MIT, see facades_mesa.inc). Materials here only carry colours: every prop is captured into
 * vertex coloured chunks by city.c and never rendered as itself. */
#define cw_sin sinf
#define cw_cos cosf
#define cw_atan2 atan2f
#define TEX_CONCRETE 0
#define TEX_WOOD 1

static t3_color lin(float r, float g, float b) {
  float c[3] = {r, g, b};
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : powf((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){c[0], c[1], c[2]};
}
typedef struct whimsy_parts {
  t3_geometry *box, *ball, *cone, *cyl, *ring;
  t3_material *red, *cream, *gold, *blue, *green, *pink, *brown, *black, *glass, *lamp;
} whimsy_parts;

typedef struct mb { float *pos, *nor, *uv, *col; uint32_t *idx; int nv, ni, cv, ci; bool colors; } mb;
static int mb_v(mb *b, t3_vec3 p, t3_vec3 n, float u, float v, float shade) {
  if (b->nv == b->cv) {
    b->cv = b->cv ? b->cv * 2 : 256;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)b->cv);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)b->cv);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->cv);
    b->col = realloc(b->col, sizeof(float) * 3 * (size_t)b->cv);
  }
  float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
  if (l < 1e-6f) l = 1;
  float *P = b->pos + b->nv * 3, *N = b->nor + b->nv * 3, *U = b->uv + b->nv * 2, *C = b->col + b->nv * 3;
  P[0] = p.x, P[1] = p.y, P[2] = p.z;
  N[0] = n.x / l, N[1] = n.y / l, N[2] = n.z / l;
  U[0] = u, U[1] = v;
  C[0] = C[1] = C[2] = shade;
  return b->nv++;
}
static void mb_t(mb *b, int a, int c, int d) {
  if (b->ni + 3 > b->ci) { b->ci = b->ci ? b->ci * 2 : 768; b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)b->ci); }
  b->idx[b->ni++] = (uint32_t)a; b->idx[b->ni++] = (uint32_t)c; b->idx[b->ni++] = (uint32_t)d;
}
static void set_attr(t3_geometry *g, t3_attr_slot s, t3_attribute *a) { t3_geometry_set_attribute(g, s, a); t3_release(a); }
static t3_geometry *mb_done(mb *b) {
  t3_geometry *g = t3_geometry_new();
  set_attr(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3));
  set_attr(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, b->nor, b->nv, 3));
  set_attr(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2));
  if (b->colors) set_attr(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, b->col, b->nv, 3));
  t3_attribute *ix = t3_attribute_new(T3_UINT32, b->idx, b->ni, 1);
  t3_geometry_set_index(g, ix); t3_release(ix);
  free(b->pos); free(b->nor); free(b->uv); free(b->col); free(b->idx);
  memset(b, 0, sizeof *b);
  return g;
}
static t3_mesh *add(t3_object *root, t3_geometry *g, t3_material *m, bool cast) {
  (void)cast;
  t3_mesh *me = t3_mesh_new(g, m);
  t3_object_add(root, me);
  t3_release(me);
  return me;
}
static t3_mesh *prop(t3_object *root, t3_geometry *geo, t3_material *m, float x, float y, float z, float sx, float sy, float sz) {
  t3_mesh *o = add(root, geo, m, true);
  o->base.position = (t3_vec3){x, y, z};
  o->base.scale = (t3_vec3){sx, sy, sz};
  return o;
}
/* a colour holder: the physical material has every field the ported code touches */
static t3_material *toy_paint(float r, float g, float b) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = lin(r, g, b);
  return m;
}
static t3_material *mat(int tex, const float c[3], float rx, float ry, float nscale, float rough) {
  (void)tex; (void)rx; (void)ry; (void)nscale; (void)rough;
  return toy_paint(c[0], c[1], c[2]);
}
/* a lamp: city.c puts basic materials in the chunk whose brightness follows the clock */
static t3_material *glow(float r, float g, float b) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = lin(r, g, b);
  return m;
}
static void circus_quad(mb *b, t3_vec3 a, t3_vec3 c, t3_vec3 d, t3_vec3 e) {
  t3_vec3 u = {c.x - a.x, c.y - a.y, c.z - a.z}, v = {d.x - a.x, d.y - a.y, d.z - a.z};
  t3_vec3 n = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
  int k = mb_v(b, a, n, 0, 0, 1); mb_v(b, c, n, 1, 0, 1); mb_v(b, d, n, 1, 1, 1); mb_v(b, e, n, 0, 1, 1);
  mb_t(b, k, k + 1, k + 2); mb_t(b, k, k + 2, k + 3);
}
static t3_material *FAC_BRICK;
static t3_geometry *FAC_PLANE;   /* a unit plane facing +z; signs scale it */
static const sign_rect *sign_find(const char *key) {
  for (int i = 0; i < SIGN_N; i++) if (!strcmp(SIGNS[i].key, key)) return &SIGNS[i];
  ds_log("city: no sign %s", key);
  return NULL;
}
static t3_material *FAC_SIGNMAT;
static void mesa_sign(t3_object *root, const char *key, float x, float y, float z, float w, float h) {
  const sign_rect *r = sign_find(key);
  if (!r) return;
  t3_mesh *m = prop(root, FAC_PLANE, FAC_SIGNMAT, x, y, z, w, h, 1);
  m->base.user_data = (void *)r;
}
#define mesa_logo mesa_sign
#define mesa_name mesa_sign
static void mesa_masonry(t3_object *root, t3_material *brick, float x, float y, float z, float w, float h, float d) {
  t3_geometry *g = t3_box_geometry_new(w, h, d, 1, 1, 1);
  float *pos = g->attributes[T3_ATTR_POSITION]->array;
  float *normal = g->attributes[T3_ATTR_NORMAL]->array;
  float *uv = g->attributes[T3_ATTR_UV]->array;
  for (int i = 0; i < g->attributes[T3_ATTR_POSITION]->count; i++) {
    float horizontal = fabsf(normal[i * 3]) > .5f ? pos[i * 3 + 2] : pos[i * 3];
    float vertical = fabsf(normal[i * 3 + 1]) > .5f ? pos[i * 3 + 2] : pos[i * 3 + 1];
    uv[i * 2] = horizontal / .88f; uv[i * 2 + 1] = vertical / .64f;
  }
  t3_mesh *m = add(root, g, brick, true); m->base.position = (t3_vec3){x, y, z};
  t3_release(g);
}

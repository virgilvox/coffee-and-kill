/* world.c: the stages, the clock and the collision queries. The lab (assets/models/lab.glb, made by
 * tools/prep-lab.mjs) collides by its triangles, in a grid of columns; the city (city.c) and the
 * arenas (arena.c) have their own simpler shapes. One hemisphere light, one directional. */
#include "world.h"
#include "city.h"
#include "kharena.h"
#include "gen/lab_areas.h"
#include <stdlib.h>
#include <string.h>

/* ── the lab's triangles ── */
#define CELL 2.f
#define LGX0 (-4.f)
#define LGZ0 (-42.f)
#define LGX 7
#define LGZ 24
typedef struct { v3 a, b, c, n; float ymin, ymax; } tri_t;
static tri_t *tris; static int ntri;
/* the camera probe: world_cam_clear sees only the building (walls and the ceiling, which run past
 * CAM_PROP_TOP); furniture and shelving below it never pull the lens in. world_cam_clear_all sees
 * everything, for the follow camera to rise over shelves instead. */
static bool cam_probe;
#define CAM_PROP_TOP 2.3f

static int *cell_start, *cell_list;
static uint32_t *stamp, stamp_now;
static t3_scene *SC;
static t3_object *town, *lab_root, *city_root, *arena_root;
static t3_light *hemi, *sun;
static world_stage stage = STAGE_TOWN;

/* meshes of the lab, for the band culling */
typedef struct { t3_mesh *m; float z, z0, z1; int tris; bool always; int twin; bool far; const char *name; } lab_mesh;  /* twin: the band's far version */
static lab_mesh lmesh[48]; static int nlmesh;
static t3_object *door_pivot[2];
static float door_open, door_target; static bool door_auto = true;
static float door_call;   /* seconds the automatic door stays called by someone walking at it (world_move) */
static t3_material *lab_mat, *lab_glow_mat, *lab_glass_mat;

static inline int cx_of(float x) { int i = (int)floorf((x - LGX0) / CELL); return i < 0 ? 0 : i >= LGX ? LGX - 1 : i; }
static inline int cz_of(float z) { int i = (int)floorf((z - LGZ0) / CELL); return i < 0 ? 0 : i >= LGZ ? LGZ - 1 : i; }

static void take_tris(t3_mesh *m) {
  t3_geometry *g = m->geometry;
  const float *e = m->base.matrix_world.e;
  const float *P = t3_attribute_f32(g->attributes[T3_ATTR_POSITION]);
  int n = g->index ? g->index->count : g->attributes[T3_ATTR_POSITION]->count;
  for (int i = 0; i + 2 < n; i += 3) {
    v3 q[3];
    for (int j = 0; j < 3; j++) {
      int k;
      if (!g->index) k = i + j;
      else if (g->index->type == T3_UINT32) k = (int)((uint32_t *)g->index->array)[i + j];
      else k = ((uint16_t *)g->index->array)[i + j];
      float x = P[k * 3], y = P[k * 3 + 1], z = P[k * 3 + 2];
      q[j] = v3_(e[0] * x + e[4] * y + e[8] * z + e[12], e[1] * x + e[5] * y + e[9] * z + e[13], e[2] * x + e[6] * y + e[10] * z + e[14]);
    }
    v3 nn = v3_cross(v3_sub(q[1], q[0]), v3_sub(q[2], q[0]));
    if (v3_dot(nn, nn) < 1e-10f) continue;
    tri_t *t = &tris[ntri++];
    t->a = q[0]; t->b = q[1]; t->c = q[2]; t->n = v3_norm(nn);
    t->ymin = fminf(q[0].y, fminf(q[1].y, q[2].y)); t->ymax = fmaxf(q[0].y, fmaxf(q[1].y, q[2].y));
  }
}
static void build_grid(void) {
  cell_start = calloc((size_t)LGX * LGZ + 1, sizeof *cell_start);
  for (int pass = 0; pass < 2; pass++) {
    int *fill = pass ? calloc((size_t)LGX * LGZ, sizeof *fill) : NULL;
    for (int i = 0; i < ntri; i++) {
      tri_t *t = &tris[i];
      int x0 = cx_of(fminf(t->a.x, fminf(t->b.x, t->c.x))), x1 = cx_of(fmaxf(t->a.x, fmaxf(t->b.x, t->c.x)));
      int z0 = cz_of(fminf(t->a.z, fminf(t->b.z, t->c.z))), z1 = cz_of(fmaxf(t->a.z, fmaxf(t->b.z, t->c.z)));
      for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
        int c = z * LGX + x;
        if (!pass) cell_start[c + 1]++; else cell_list[cell_start[c] + fill[c]++] = i;
      }
    }
    if (!pass) { for (int c = 0; c < LGX * LGZ; c++) cell_start[c + 1] += cell_start[c]; cell_list = malloc((size_t)cell_start[LGX * LGZ] * sizeof *cell_list); }
    free(fill);
  }
  stamp = calloc((size_t)ntri, sizeof *stamp);
}
static void count_tris(t3_object *o, void *ctx) {
  if (o->type != T3_MESH) return;
  t3_geometry *g = ((t3_mesh *)o)->geometry;
  *(int *)ctx += (g->index ? g->index->count : g->attributes[T3_ATTR_POSITION]->count) / 3;
}
static void load_lab(void) {
  size_t n; uint8_t *b = ds_asset("lab.glb", &n);
  if (!b) { ds_log("world: lab.glb missing"); return; }
  t3_gltf *g = t3_gltf_parse(b, n, NULL, NULL);
  free(b);
  if (!g) { ds_log("world: bad lab.glb"); return; }
  t3_object *root = g->scene;
  lab_mat = t3_mesh_lambert_material_new(0xffffff); lab_mat->vertex_colors = true; lab_mat->side = T3_DOUBLE_SIDE;
  lab_glow_mat = t3_mesh_basic_material_new(0xffffff); lab_glow_mat->vertex_colors = true;
  lab_glass_mat = t3_mesh_basic_material_new(0xffffff); lab_glass_mat->vertex_colors = true; lab_glass_mat->transparent = true; lab_glass_mat->opacity = .1f; lab_glass_mat->side = T3_DOUBLE_SIDE; lab_glass_mat->depth_write = false;
  t3_object_update_matrix_world(root, true);
  int count = 0;
  t3_object_traverse(root, count_tris, &count);
  tris = malloc((size_t)count * sizeof *tris);
  /* the children of the scene: named chunks. Doors move, so they stay out of the triangles */
  int nc = root->child_count;
  t3_object *kids[80];
  for (int i = 0; i < nc && i < 80; i++) kids[i] = root->children[i];
  for (int i = 0; i < nc && i < 80; i++) {
    t3_object *o = kids[i];
    t3_mesh *m = NULL;
    if (o->type == T3_MESH) m = (t3_mesh *)o;
    else if (o->child_count && o->children[0]->type == T3_MESH) m = (t3_mesh *)o->children[0];
    if (!m) continue;
    const char *name = o->name ? o->name : "";
    bool door = !strncmp(name, "door_", 5), glass = strstr(name, "glass") != NULL, gl = strstr(name, "glow") || strstr(name, "lamps");
    { t3_material *nm = glass ? lab_glass_mat : gl ? lab_glow_mat : lab_mat; t3_retain(nm); t3_release(m->materials[0]); m->materials[0] = nm; }
    if (!door) {
      bool far = strstr(name, "_far") != NULL;
      if (strcmp(name, "lab_soft") && !far) take_tris(m);
      if (nlmesh < 48) {
        t3_geometry_compute_bounding_sphere(m->geometry);
        float cz = m->geometry->bounding_sphere.center.z + o->position.z;
        /* a band's z span: bands are 4 m of plan x, world z = -plan x */
        int bi = !strncmp(name, "lab_b", 5) ? atoi(name + 5) : -1;
        float z1 = bi >= 0 ? -bi * 4.f : cz + 2, z0 = bi >= 0 ? z1 - 4 : cz - 2;
        lmesh[nlmesh++] = (lab_mesh){m, cz, z0, z1, m->geometry->index ? m->geometry->index->count / 3 : 0, !strcmp(name, "lab_shell") || !strcmp(name, "lab_glass_front") || !strcmp(name, "lab_slab"), -1, far, name};
      }
    }
  }
  /* pair each band with its far twin */
  for (int i = 0; i < nlmesh; i++) if (lmesh[i].far) {
    const char *fn = lmesh[i].name;
    for (int k = 0; k < nlmesh; k++) { const char *nn = lmesh[k].name; if (!lmesh[k].far && !strncmp(nn, fn, 7) && strlen(nn) == 7) lmesh[k].twin = i; }
  }
  build_grid();
  t3_object_add(lab_root, root);
  /* door leaves: hinges at the jambs, plan z 1.88 and 3.67 (world x -0.895 and 0.895), plan x 5.45 */
  float hx[2] = {1.88f - LAB_DOOR_Z, 3.67f - LAB_DOOR_Z}, hz = -LAB_DOOR_X;
  for (int s = 0; s < 2; s++) {
    door_pivot[s] = t3_group_new();
    t3_object_set_position(door_pivot[s], hx[s], 0, hz);
    t3_object_add(root, door_pivot[s]);
    for (int i = 0; i < nc && i < 64; i++) {
      t3_object *o = kids[i];
      const char *name = o->name ? o->name : "";
      if (strncmp(name, s ? "door_b" : "door_a", 6)) continue;
      t3_retain(o);
      t3_object_remove(root, o);
      o->position.x -= hx[s]; o->position.z -= hz;
      t3_object_add(door_pivot[s], o);
      t3_release(o);
    }
  }
  ds_log("world: lab %d triangles, %d cell refs", ntri, cell_start[LGX * LGZ]);
}

/* ── the sky ── */
#define DOME_R 390.f
#define D_W 28
#define D_H 12
static t3_mesh *dome, *sundisc, *stars;
static t3_geometry *dome_g;
static int dome_rows;      /* vertices of the gradient, then the skyline's */
static t3_material *dome_m, *sun_m, *star_m;
static float *dome_dir;    /* unit direction per vertex; skyline vertices marked by w */
static int dome_nv;
#include "gen/city_data.h"
static void build_sky(t3_scene *scene) {
  int nv_grad = (D_W + 1) * (D_H + 1);
  int nsky = CITY_NSKY, nv = nv_grad + nsky * 8, ni = D_W * D_H * 6 + nsky * 18;
  float *pos = calloc((size_t)nv * 3, sizeof(float)), *col = calloc((size_t)nv * 3, sizeof(float));
  dome_dir = calloc((size_t)nv * 4, sizeof(float));
  uint32_t *ix = malloc(sizeof(uint32_t) * (size_t)ni);
  int k = 0, q = 0;
  for (int j = 0; j <= D_H; j++) for (int i = 0; i <= D_W; i++) {
    /* more rows near the horizon; a skirt below it */
    float t = (float)j / D_H, el = t < .17f ? -.25f + t * 1.5f : (t - .17f) * (t - .17f) * 1.9f + (t - .17f) * .35f;
    el = fminf(el, 1.5707f);
    float az = i * 6.28318f / D_W;
    float cx = cosf(el) * cosf(az), cy = sinf(el), cz = cosf(el) * sinf(az);
    dome_dir[k * 4] = cx; dome_dir[k * 4 + 1] = cy; dome_dir[k * 4 + 2] = cz; dome_dir[k * 4 + 3] = 0;
    pos[k * 3] = cx * DOME_R; pos[k * 3 + 1] = cy * DOME_R; pos[k * 3 + 2] = cz * DOME_R;
    k++;
  }
  for (int j = 0; j < D_H; j++) for (int i = 0; i < D_W; i++) {
    int a = j * (D_W + 1) + i, b = a + 1, c = a + D_W + 1, d = c + 1;
    ix[q++] = a; ix[q++] = c; ix[q++] = b; ix[q++] = b; ix[q++] = c; ix[q++] = d;
  }
  /* the far skyline: boxes pushed out onto the dome, flat in the fog colour */
  for (int s = 0; s < nsky; s++) {
    float x = CITY_SKY[s][0] - 35, z = CITY_SKY[s][1] + 30, h = CITY_SKY[s][2], w = CITY_SKY[s][3];
    float an = atan2f(z, x), r = hypotf(x, z), dw = w / r * .9f, top = atanf(h / r) * 1.0f;
    float an0 = an - dw, an1 = an + dw;
    float v[4][2] = {{an0, -.08f}, {an1, -.08f}, {an1, top}, {an0, top}};
    int base = k;
    for (int c = 0; c < 4; c++) {
      float cxx = cosf(v[c][1]) * cosf(v[c][0]), cy = sinf(v[c][1]), czz = cosf(v[c][1]) * sinf(v[c][0]);
      dome_dir[k * 4] = cxx; dome_dir[k * 4 + 1] = cy; dome_dir[k * 4 + 2] = czz; dome_dir[k * 4 + 3] = .7f + (s % 3) * .1f;
      pos[k * 3] = cxx * (DOME_R - 2); pos[k * 3 + 1] = cy * (DOME_R - 2); pos[k * 3 + 2] = czz * (DOME_R - 2);
      k++;
    }
    ix[q++] = base; ix[q++] = base + 1; ix[q++] = base + 2; ix[q++] = base; ix[q++] = base + 2; ix[q++] = base + 3;
  }
  dome_nv = k; dome_rows = nv_grad;
  dome_g = t3_geometry_new();
  t3_attribute *ap = t3_attribute_new(T3_FLOAT32, pos, k, 3), *ac = t3_attribute_new(T3_FLOAT32, col, k, 3), *ai = t3_attribute_new(T3_UINT32, ix, q, 1);
  t3_geometry_set_attribute(dome_g, T3_ATTR_POSITION, ap); t3_geometry_set_attribute(dome_g, T3_ATTR_COLOR, ac); t3_geometry_set_index(dome_g, ai);
  t3_release(ap); t3_release(ac); t3_release(ai);
  free(pos); free(col); free(ix);
  dome_m = t3_mesh_basic_material_new(0xffffff);
  dome_m->vertex_colors = true; dome_m->side = T3_DOUBLE_SIDE; dome_m->depth_write = false; dome_m->depth_test = false; dome_m->fog = false;
  dome = t3_mesh_new(dome_g, dome_m);
  ((t3_object *)dome)->render_order = -1000; ((t3_object *)dome)->frustum_culled = false;
  t3_object_add(&scene->base, dome);
  /* the sun */
  sun_m = t3_mesh_basic_material_new(0xffffff); sun_m->depth_write = false; sun_m->depth_test = false; sun_m->fog = false;
  sundisc = t3_mesh_new(t3_sphere_geometry_new(1, 12, 8), sun_m);
  ((t3_object *)sundisc)->render_order = -999; ((t3_object *)sundisc)->frustum_culled = false;
  t3_object_add(&scene->base, sundisc);
  /* stars: little quads */
  enum { NS = 900 };
  float *sp = malloc(sizeof(float) * NS * 4 * 3); uint32_t *si = malloc(sizeof(uint32_t) * NS * 6);
  uint32_t r = 12345;
  for (int i = 0; i < NS; i++) {
    r = r * 1664525u + 1013904223u; float az = (r >> 8) * 3.74e-7f; r = r * 1664525u + 1013904223u; float y = .08f + (r >> 8) * 5.7e-8f;
    r = r * 1664525u + 1013904223u; float sz = .22f + (r >> 28) * .035f;
    float rr = sqrtf(1 - y * y), cx = rr * cosf(az), cz = rr * sinf(az);
    v3 n = v3_(cx, y, cz), u = v3_norm(v3_cross(n, v3_(0, 1, 0))), v = v3_cross(u, n);
    for (int c = 0; c < 4; c++) {
      float a = c == 0 || c == 3 ? -sz : sz, b = c < 2 ? -sz : sz;
      sp[(i * 4 + c) * 3] = n.x * (DOME_R - 4) + u.x * a + v.x * b; sp[(i * 4 + c) * 3 + 1] = n.y * (DOME_R - 4) + u.y * a + v.y * b; sp[(i * 4 + c) * 3 + 2] = n.z * (DOME_R - 4) + u.z * a + v.z * b;
    }
    uint32_t b = (uint32_t)i * 4; uint32_t f[6] = {b, b + 1, b + 2, b, b + 2, b + 3}; memcpy(si + i * 6, f, sizeof f);
  }
  t3_geometry *sg = t3_geometry_new();
  t3_attribute *a0 = t3_attribute_new(T3_FLOAT32, sp, NS * 4, 3), *a1 = t3_attribute_new(T3_UINT32, si, NS * 6, 1);
  t3_geometry_set_attribute(sg, T3_ATTR_POSITION, a0); t3_geometry_set_index(sg, a1); t3_release(a0); t3_release(a1);
  free(sp); free(si);
  star_m = t3_mesh_basic_material_new(0xffffff); star_m->depth_write = false; star_m->depth_test = false; star_m->fog = false; star_m->side = T3_DOUBLE_SIDE;
  stars = t3_mesh_new(sg, star_m); t3_release(sg);
  ((t3_object *)stars)->render_order = -998; ((t3_object *)stars)->frustum_culled = false;
  t3_object_add(&scene->base, stars);
}
static t3_color city_glow; /* the downtown's sodium glow on the low sky after dark, added by sky_paint */
static t3_color sky_mid; static float sky_mid_w;  /* a clear light blue between the horizon and the zenith by day */
static void sky_paint(t3_color zen, t3_color hor, t3_color glow, v3 sd, float glow_amt, t3_color skyline) {
  float *c = t3_attribute_f32(dome_g->attributes[T3_ATTR_COLOR]);
  for (int i = 0; i < dome_nv; i++) {
    float x = dome_dir[i * 4], y = dome_dir[i * 4 + 1], z = dome_dir[i * 4 + 2], w = dome_dir[i * 4 + 3];
    if (w > 0) { c[i * 3] = skyline.r * w; c[i * 3 + 1] = skyline.g * w; c[i * 3 + 2] = skyline.b * w; continue; }
    float t = powf(ds_clamp(y, 0, 1), .55f);
    float r = ds_lerp(hor.r, zen.r, t), g = ds_lerp(hor.g, zen.g, t), b = ds_lerp(hor.b, zen.b, t);
    if (sky_mid_w > 0) {
      /* horizon to light blue to zenith, so a warm horizon never greys the middle of the sky */
      t3_color m = {ds_lerp((hor.r + zen.r) * .5f, sky_mid.r, sky_mid_w), ds_lerp((hor.g + zen.g) * .5f, sky_mid.g, sky_mid_w), ds_lerp((hor.b + zen.b) * .5f, sky_mid.b, sky_mid_w)};
      float u = t < .3f ? t / .3f : (t - .3f) / .7f;
      u = u * u * (3 - 2 * u);
      if (t < .3f) { r = ds_lerp(hor.r, m.r, u); g = ds_lerp(hor.g, m.g, u); b = ds_lerp(hor.b, m.b, u); }
      else { r = ds_lerp(m.r, zen.r, u); g = ds_lerp(m.g, zen.g, u); b = ds_lerp(m.b, zen.b, u); }
    }
    float d = fmaxf(0, x * sd.x + y * sd.y + z * sd.z);
    float gl = (powf(d, 8) * .22f + powf(d, 60) * .55f) * glow_amt;
    float cg = powf(1 - ds_clamp(y * 3.5f, 0, 1), 3);
    c[i * 3] = r + glow.r * gl + city_glow.r * cg; c[i * 3 + 1] = g + glow.g * gl + city_glow.g * cg; c[i * 3 + 2] = b + glow.b * gl + city_glow.b * cg;
  }
  dome_g->attributes[T3_ATTR_COLOR]->version++;
}

/* ── the clock ── */
typedef struct { float h; uint32_t zen, hor, fog; uint32_t hsky, hgnd; float hi; uint32_t sc; float si; float fnear, ffar; float night; } tod_key;
/* intensities are three.js physical: pi is one to one on a white surface facing the light */
static const tod_key TOD[] = {
  { 9.0f, 0x245ec4, 0xbcd8f0, 0xc4d6e4, 0xdbe8ff, 0x9c8a72, 1.75f, 0xfff0d6, 1.55f, 110, 470, 0},
  {12.0f, 0x1a52c4, 0xaed0f2, 0xbcd2e8, 0xe2ecff, 0xa4927a, 1.85f, 0xfff6e8, 1.6f, 110, 480, 0},
  {15.5f, 0x1e58c2, 0xbcd8f0, 0xc4d6e4, 0xe4e8f4, 0xa08c74, 1.75f, 0xfff0d8, 1.5f, 110, 470, 0},
  {16.6f, 0x2458b8, 0xf0d8b0, 0xe4d4bc, 0xf2e2cc, 0xa48464, 1.65f, 0xffd8a0, 1.6f, 100, 460, 0},
  {17.4f, 0x2c55a6, 0xf8c07c, 0xecc296, 0xf0d8bc, 0xa07a5a, 1.5f, 0xffbc78, 1.65f, 90, 450, 0.03f},
  {18.3f, 0x3c4e94, 0xffa468, 0xe89a6c, 0xd8b4ac, 0x7c5850, 1.3f, 0xffa060, 1.5f, 70, 430, 0.25f},
  {19.0f, 0x232c66, 0xc9607c, 0x8c5a7a, 0x8a84b8, 0x4a3c50, 1.25f, 0xb06060, .35f, 55, 380, 0.7f},
  {20.0f, 0x10163a, 0x3a3060, 0x2c2a50, 0x6a72b4, 0x3a3648, 1.5f, 0x7082c0, .35f, 45, 340, 1},
  {24.0f, 0x070a1c, 0x161a34, 0x12162c, 0x5a64a8, 0x2e2e40, 1.4f, 0x6c7cc0, .35f, 40, 320, 1},
};
#define NTOD (int)(sizeof TOD / sizeof TOD[0])
static float clock_h = 12, in_lab_w = 0, lab_night;
static t3_color cl(t3_color a, t3_color b, float t) { t3_color c = {ds_lerp(a.r, b.r, t), ds_lerp(a.g, b.g, t), ds_lerp(a.b, b.b, t)}; return c; }
static t3_color cs(t3_color a, float s) { t3_color c = {a.r * s, a.g * s, a.b * s}; return c; }
/* the warm lights inside the lab */
static const float LAB_H = 1.7f, LAB_D = .55f;
static v3 sun_at = {0, 1, 0};
static float dome_stamp = -1; static world_stage dome_stage = STAGE_COUNT;
static float dome_blend = -1;
static void apply_lights(void) {
  if (stage != STAGE_TOWN) {
    kharena_mood m; kharena_mood_of(stage, &m);
    if (dome_stage != stage || dome_blend != kharena_blend()) {
      dome_blend = kharena_blend();
      sky_paint(m.zenith, m.horizon, m.sun, m.sun_dir, .35f, cs(m.fog, .9f));
      dome_stage = stage; dome_stamp = -1;
      sun_m->color = m.sun; sun_m->version++;
    }
    hemi->color = m.hemi_sky; hemi->ground_color = m.hemi_ground; hemi->intensity = m.hemi;
    sun->color = m.sun; sun->intensity = m.dir;
    t3_object_set_position(sun, m.sun_dir.x * 100, m.sun_dir.y * 100, m.sun_dir.z * 100);
    SC->fog = (t3_fog){T3_FOG_LINEAR, m.fog, m.fog_near, m.fog_far, 0};
    SC->has_background = true; SC->background = m.horizon;
    ((t3_object *)stars)->visible = m.stars > .01f;
    star_m->color = (t3_color){m.stars, m.stars, m.stars}; star_m->version++;
    sun_at = m.sun_dir;
    ((t3_object *)sundisc)->scale = t3_v3(9, 9, 9);
    return;
  }
  float h = ds_clamp(clock_h, TOD[0].h, TOD[NTOD - 1].h);
  int i = 0; while (i + 2 < NTOD && h > TOD[i + 1].h) i++;
  const tod_key *A = &TOD[i], *B = &TOD[i + 1];
  float t = ds_clamp((h - A->h) / (B->h - A->h), 0, 1);
  t = t * t * (3 - 2 * t);
#define MIX(f) cl(t3_color_hex(A->f), t3_color_hex(B->f), t)
  t3_color zen = MIX(zen), hor = MIX(hor), fog = MIX(fog), hsky = MIX(hsky), hgnd = MIX(hgnd), sc = MIX(sc);
  float hi = ds_lerp(A->hi, B->hi, t), si = ds_lerp(A->si, B->si, t), fn = ds_lerp(A->fnear, B->fnear, t), ff = ds_lerp(A->ffar, B->ffar, t), night = ds_lerp(A->night, B->night, t);
  /* the sun: east in the morning, a high arc through the south, west at dusk; after sunset the moon stands in */
  const float RISE = 6.8f, SET = 18.7f;
  float dayf = (h - RISE) / (SET - RISE);
  v3 sd;
  if (dayf <= 1) {
    float az = dayf * DS_PI, el = sinf(dayf * DS_PI) * 1.05f + .02f;
    sd = v3_(cosf(az) * cosf(el), sinf(el), .55f * sinf(az) * cosf(el));
    sd = v3_norm(sd);
  } else {
    sd = v3_norm(v3_(-.45f, .75f, .35f));   /* the moon, high */
  }
  /* the light itself cannot sit below the horizon: keep it a grazing glow */
  v3 ld = sd; if (ld.y < .1f) { ld.y = .1f; ld = v3_norm(ld); }
  /* inside the lab the lights are its own: warm and even. Scene lights blend over, and the materials
   * outside compensate so the street through the glass keeps the street's light. */
  float w = in_lab_w;
  t3_color lab_sky = t3_color_hex(0xfff2dc), lab_gnd = t3_color_hex(0xb09c84), lab_dir = t3_color_hex(0xffe8c8);
  t3_color Hs = cl(cs(hsky, hi), cs(lab_sky, LAB_H), w), Hg = cl(cs(hgnd, hi), cs(lab_gnd, LAB_H), w), Ds = cl(cs(sc, si), cs(lab_dir, LAB_D), w);
  hemi->color = Hs; hemi->ground_color = Hg; hemi->intensity = 1;
  sun->color = Ds; sun->intensity = 1;
  v3 dirv = v3_norm(v3_lerp(ld, v3_norm(v3_(.15f, 1, .25f)), w));
  t3_object_set_position(sun, dirv.x * 100, dirv.y * 100, dirv.z * 100);
  SC->fog = (t3_fog){T3_FOG_LINEAR, fog, fn, ff, 0};
  SC->has_background = true; SC->background = hor;
  /* the street's lit materials hold the outdoor light when the scene lights are the lab's */
  t3_color out_avg = {(hsky.r * hi * 1.15f + sc.r * si * .35f), (hsky.g * hi * 1.15f + sc.g * si * .35f), (hsky.b * hi * 1.15f + sc.b * si * .35f)};
  t3_color cur_avg = {Hs.r * 1.15f + Ds.r * .35f, Hs.g * 1.15f + Ds.g * .35f, Hs.b * 1.15f + Ds.b * .35f};
  t3_color tint = {out_avg.r / cur_avg.r, out_avg.g / cur_avg.g, out_avg.b / cur_avg.b};
  city_set_time(night, tint);
  lab_night = night;
  /* the lab's lit material holds the warm light when the scene lights are the street's */
  t3_color in_avg = {lab_sky.r * LAB_H * 1.15f + lab_dir.r * LAB_D * .35f, lab_sky.g * LAB_H * 1.15f + lab_dir.g * LAB_D * .35f, lab_sky.b * LAB_H * 1.15f + lab_dir.b * LAB_D * .35f};
  if (lab_mat) { lab_mat->color = (t3_color){in_avg.r / cur_avg.r, in_avg.g / cur_avg.g, in_avg.b / cur_avg.b}; lab_mat->version++; }
  /* the sky, which costs an upload: only when it has moved */
  if (fabsf(h - dome_stamp) > .02f || dome_stage != STAGE_TOWN) {
    dome_stamp = h; dome_stage = STAGE_TOWN;
    t3_color skyline = cl(fog, hor, .35f);
    float glow_amt = dayf <= 1 ? ds_clamp(1.35f - sd.y * 1.6f, .12f, 1.2f) : 0;
    t3_color glowc = cl(sc, t3_color_hex(0xffe0b0), .3f);
    float cgw = ds_clamp((night - .3f) * 1.5f, 0, 1);
    city_glow = (t3_color){.16f * cgw, .075f * cgw, .03f * cgw};
    sky_mid = t3_color_hex(0x5e9ce6); sky_mid_w = .8f * (1 - ds_clamp(night * 4, 0, 1));
    sky_paint(zen, hor, glowc, sd, glow_amt, skyline);
    city_glow = (t3_color){0, 0, 0}; sky_mid_w = 0;
    t3_color sunc = dayf <= 1 ? cs(cl(sc, t3_color_hex(0xffffff), ds_clamp(sd.y * 2, 0, .85f)), 1.6f) : (t3_color){.9f, .92f, 1.05f};
    sun_m->color = sunc; sun_m->version++;
    float sr = dayf <= 1 ? 5.5f + (1 - ds_clamp(sd.y * 3, 0, 1)) * 3.5f : 6.f;
    ((t3_object *)sundisc)->scale = t3_v3(sr, sr, sr);
    ((t3_object *)sundisc)->visible = dayf <= 1.03f || h > SET + .9f;
    sun_at = sd;
    float st = ds_clamp((night - .55f) * 2.2f, 0, 1);
    ((t3_object *)stars)->visible = st > .01f;
    star_m->color = (t3_color){st * .9f, st * .9f, st}; star_m->version++;
  }
}
void world_set_time(float hours) { clock_h = hours; if (hemi) apply_lights(); }

/* ── stages ── */
void world_init(t3_scene *scene) {
  SC = scene;
  town = t3_group_new(); lab_root = t3_group_new(); city_root = t3_group_new(); arena_root = t3_group_new();
  t3_object_add(&scene->base, town); t3_object_add(town, lab_root); t3_object_add(town, city_root); t3_object_add(&scene->base, arena_root);
  double t0 = ds_now();
  hemi = t3_hemisphere_light_new(0xffffff, 0xa09080, 1.8f); t3_object_add(&scene->base, hemi);
  sun = t3_directional_light_new(0xffffff, 1.5f); t3_object_add(&scene->base, sun);
  load_lab();
  city_init(city_root);
  build_sky(scene);
  kharena_init(arena_root);
  world_set_stage(STAGE_TOWN);
  world_set_time(clock_h);
  (void)t0;
}
void world_set_stage(world_stage s) {
  stage = s;
  town->visible = s == STAGE_TOWN;
  kharena_show(s);
  dome_stage = STAGE_COUNT;
  apply_lights();
}
world_stage world_get_stage(void) { return stage; }
void world_stage_floor(world_stage s, v3 *center, float *radius) {
  if (s == STAGE_TOWN) { city_place("mid_block", center, NULL); if (radius) *radius = 5.5f; return; }
  kharena_floor(s, center, radius);
}

/* ── per frame ── */
#define LAB_X0 (-LAB_DOOR_Z - .1f)
#define LAB_X1 (9.5f - LAB_DOOR_Z)
bool world_in_lab(v3 p) { return p.x > LAB_X0 && p.x < LAB_X1 && p.z < -LAB_DOOR_X + .1f && p.z > -40.5f; }
/* under the portico, in front of the glass and in the recessed entry: everything inside is in view */
static bool at_storefront(v3 p) { return p.x > LAB_X0 - .5f && p.x < LAB_X1 + .5f && p.z < 1.5f && p.z > -LAB_DOOR_X - .5f && p.y < 4; }
static v3 subj_p; static float subj_t;
void world_subject(v3 p) { subj_p = p; subj_t = .5f; }
void world_update(float dt, v3 focus) {
  t3_object_set_position(dome, focus.x, focus.y, focus.z);
  t3_object_set_position(stars, focus.x, focus.y, focus.z);
  t3_object_set_position(sundisc, sun_at.x * (DOME_R - 6) + focus.x, sun_at.y * (DOME_R - 6) + focus.y, sun_at.z * (DOME_R - 6) + focus.z);
  if (stage != STAGE_TOWN) { if (kharena_update(dt)) apply_lights(); return; }
  /* the door */
  /* the subject, else the lens */
  bool has_subj = subj_t > 0; subj_t -= dt;
  v3 subj = has_subj ? subj_p : focus;
  /* the automatic door: called by anyone walking at it or standing in it (the player, walkers: world_move
   * notes them), or by the subject of a scene close to it. Not by the lens: a camera does not open doors */
  door_call -= dt;
  if (door_auto) door_target = (door_call > 0 || (has_subj && hypotf(subj.x, subj.z + LAB_DOOR_X) < 2.6f)) ? 1.f : 0.f;
  door_open = ds_damp(door_open, door_target, door_target > door_open ? 12.f : 5.f, dt);
  if (door_pivot[0]) {
    t3_object_set_rotation(door_pivot[0], 0, -door_open * 1.55f, 0);
    t3_object_set_rotation(door_pivot[1], 0, door_open * 1.55f, 0);
  }
  /* the lab's screens and LEDs: brighter against the dark, and a little alive */
  if (lab_glow_mat) {
    static float gt; gt += dt;
    float k = (1 + .35f * lab_night) * (1 + .04f * sinf(gt * 3.1f) * sinf(gt * .7f));
    lab_glow_mat->color = (t3_color){k, k, k}; lab_glow_mat->version++;
  }
  /* lights: blend to the lab's inside */
  /* the lights are the subject's: someone inside is lit by the lab wherever the lens stands */
  bool in = world_in_lab(focus), in_s = world_in_lab(subj);
  float nw = ds_damp(in_lab_w, in_s ? 1.f : 0.f, 5, dt);
  if (fabsf(nw - in_lab_w) > .001f) { in_lab_w = nw; apply_lights(); } else if (in_lab_w != (in_s ? 1.f : 0.f)) { in_lab_w = in_s ? 1.f : 0.f; apply_lights(); }
  /* far from the lab it does not draw, deep inside the city does not */
  float dl = hypotf(ds_clamp(focus.x, LAB_X0, LAB_X1) - focus.x, ds_clamp(focus.z, -40.5f, 0) - focus.z);
  /* the interior draws whole when the lens is in it, at the glass or in the entry, or near the building
   * with the subject inside (a lens out the back door, through a window). It used to key on the lens
   * alone, and a lens between the glass and the doors saw an empty box */
  bool open = in || at_storefront(focus) || (in_s && dl < 15);
  /* inside, a band more than LAB_NEAR away along the hall draws its far twin. Outside, only from in front of
   * the glass: the first bands near, the next ones far */
  const float LAB_NEAR = 6.5f;
  bool facing = focus.z > -1.f && fabsf(focus.x - (LAB_X0 + LAB_X1) * .5f) < 70;
  for (int i = 0; i < nlmesh; i++) {
    lab_mesh *L = &lmesh[i];
    if (L->far) continue;
    bool on, near = true;
    float dz = focus.z > L->z1 ? focus.z - L->z1 : focus.z < L->z0 ? L->z0 - focus.z : 0;
    if (L->always) on = dl < 260;
    else if (open) { on = true; near = dz < LAB_NEAR; }
    else if (!facing) on = false;
    else { on = L->z > -16.f && dl < 60; near = L->z > -8.f && dl < 11 && fabsf(focus.x - (LAB_X0 + LAB_X1) * .5f) < 9; }
    if (L->twin < 0) { ((t3_object *)L->m)->visible = on; continue; }
    ((t3_object *)L->m)->visible = on && near;
    ((t3_object *)lmesh[L->twin].m)->visible = on && !near;
  }
  if (door_pivot[0]) { door_pivot[0]->visible = dl < 120; door_pivot[1]->visible = dl < 120; }
  /* from inside the lab only the storefront looks out: the street within a block or so */
  city_update(dt, focus, !in ? 1e9f : focus.z < -14 ? 70.f : 150.f);
}
void world_debug_counts(int *draws, int *tri) { int t = 0; int d = city_stats(&t); if (stage == STAGE_TOWN) for (int i = 0; i < nlmesh; i++) if (((t3_object *)lmesh[i].m)->visible) { d++; t += lmesh[i].tris; } if (draws) *draws = d; if (tri) *tri = t; }

/* ── lab queries ── */
static v3 closest_on_tri(v3 p, const tri_t *t) {
  v3 a = t->a, b = t->b, c = t->c;
  v3 ab = v3_sub(b, a), ac = v3_sub(c, a), ap = v3_sub(p, a);
  float d1 = v3_dot(ab, ap), d2 = v3_dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return a;
  v3 bp = v3_sub(p, b);
  float d3 = v3_dot(ab, bp), d4 = v3_dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return b;
  float vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return v3_add(a, v3_scale(ab, d1 / (d1 - d3)));
  v3 cp = v3_sub(p, c);
  float d5 = v3_dot(ab, cp), d6 = v3_dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return c;
  float vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return v3_add(a, v3_scale(ac, d2 / (d2 - d6)));
  float va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return v3_add(b, v3_scale(v3_sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
  float den = 1 / (va + vb + vc);
  return v3_add(a, v3_add(v3_scale(ab, vb * den), v3_scale(ac, vc * den)));
}
static inline bool near_lab(v3 p, float m) { return p.x > LGX0 - m && p.x < LGX0 + LGX * CELL + m && p.z > LGZ0 - m && p.z < 1.0f + m; }
static bool lab_push_sphere(v3 *c, float r, v3 *normal) {
  if (!tris || !near_lab(*c, r)) return false;
  bool hit = false; v3 acc = v3_(0, 0, 0);
  if (++stamp_now == 0) { memset(stamp, 0, (size_t)ntri * sizeof *stamp); stamp_now = 1; }
  int x0 = cx_of(c->x - r), x1 = cx_of(c->x + r), z0 = cz_of(c->z - r), z1 = cz_of(c->z + r);
  for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
    int cell = z * LGX + x;
    for (int k = cell_start[cell]; k < cell_start[cell + 1]; k++) {
      int i = cell_list[k];
      if (stamp[i] == stamp_now) continue;
      stamp[i] = stamp_now;
      const tri_t *t = &tris[i];
      if (cam_probe && t->ymax < CAM_PROP_TOP) continue;
      if (c->y + r < t->ymin || c->y - r > t->ymax) continue;
      v3 q = closest_on_tri(*c, t), d = v3_sub(*c, q);
      float l2 = v3_dot(d, d);
      if (l2 >= r * r) continue;
      float l = sqrtf(l2);
      v3 dir = l > 1e-5f ? v3_scale(d, 1 / l) : t->n;
      if (l <= 1e-5f && v3_dot(dir, v3_sub(*c, t->a)) < 0) dir = v3_scale(dir, -1);
      *c = v3_add(*c, v3_scale(dir, r - l));
      acc = v3_add(acc, dir); hit = true;
    }
  }
  if (normal) *normal = hit ? v3_norm(acc) : v3_(0, 1, 0);
  return hit;
}
static bool ray_tri(v3 o, v3 d, const tri_t *t, float *out) {
  v3 e1 = v3_sub(t->b, t->a), e2 = v3_sub(t->c, t->a), p = v3_cross(d, e2);
  float det = v3_dot(e1, p);
  if (fabsf(det) < 1e-9f) return false;
  float inv = 1 / det; v3 s = v3_sub(o, t->a);
  float u = v3_dot(s, p) * inv;
  if (u < 0 || u > 1) return false;
  v3 q = v3_cross(s, e1);
  float v = v3_dot(d, q) * inv;
  if (v < 0 || u + v > 1) return false;
  float tt = v3_dot(e2, q) * inv;
  if (tt < 0) return false;
  *out = tt;
  return true;
}
static bool lab_raycast(v3 o, v3 d, float max_dist, float *t_out, v3 *normal) {
  if (!tris) return false;
  /* clip the ray to the lab's box first */
  float tn = 0, tf = max_dist;
  const float lo[2] = {LGX0, LGZ0}, hi[2] = {LGX0 + LGX * CELL, 1.0f}, oo[2] = {o.x, o.z}, dd[2] = {d.x, d.z};
  for (int a = 0; a < 2; a++) {
    if (fabsf(dd[a]) < 1e-8f) { if (oo[a] < lo[a] || oo[a] > hi[a]) return false; continue; }
    float t0 = (lo[a] - oo[a]) / dd[a], t1 = (hi[a] - oo[a]) / dd[a];
    if (t0 > t1) { float q = t0; t0 = t1; t1 = q; }
    tn = fmaxf(tn, t0); tf = fminf(tf, t1);
    if (tn > tf) return false;
  }
  if (++stamp_now == 0) { memset(stamp, 0, (size_t)ntri * sizeof *stamp); stamp_now = 1; }
  v3 s = v3_add(o, v3_scale(d, tn + 1e-4f));
  float best = max_dist; int best_i = -1;
  int x = cx_of(s.x), z = cz_of(s.z);
  int sx = d.x > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
  float tdx = fabsf(d.x) > 1e-9f ? CELL / fabsf(d.x) : 1e30f, tdz = fabsf(d.z) > 1e-9f ? CELL / fabsf(d.z) : 1e30f;
  float bx = LGX0 + (x + (sx > 0)) * CELL, bz = LGZ0 + (z + (sz > 0)) * CELL;
  float tmx = fabsf(d.x) > 1e-9f ? (bx - o.x) / d.x : 1e30f, tmz = fabsf(d.z) > 1e-9f ? (bz - o.z) / d.z : 1e30f;
  for (int guard = 0; guard < 128; guard++) {
    int cell = z * LGX + x;
    for (int k = cell_start[cell]; k < cell_start[cell + 1]; k++) {
      int i = cell_list[k];
      if (stamp[i] == stamp_now) continue;
      stamp[i] = stamp_now;
      if (cam_probe && tris[i].ymax < CAM_PROP_TOP) continue;
      float tt;
      if (ray_tri(o, d, &tris[i], &tt) && tt < best) { best = tt; best_i = i; }
    }
    float t_next = fminf(tmx, tmz);
    if (best_i >= 0 && best <= t_next) break;
    if (t_next > max_dist) break;
    if (tmx < tmz) { x += sx; tmx += tdx; if (x < 0 || x >= LGX) break; }
    else { z += sz; tmz += tdz; if (z < 0 || z >= LGZ) break; }
  }
  if (best_i < 0) return false;
  *t_out = best;
  if (normal) { v3 n = tris[best_i].n; *normal = v3_dot(n, d) > 0 ? v3_scale(n, -1) : n; }
  return true;
}
/* ── public queries ── */
bool world_raycast(v3 o, v3 d, float max_dist, float *t_out, v3 *normal) {
  d = v3_norm(d);
  if (stage != STAGE_TOWN) return kharena_raycast(stage, o, d, max_dist, t_out, normal);
  float t1 = 1e30f, t2 = 1e30f; v3 n1 = v3_(0, 1, 0), n2 = n1;
  bool a = lab_raycast(o, d, max_dist, &t1, &n1), b = city_raycast(o, d, max_dist, &t2, &n2);
  if (!a && !b) return false;
  if (a && (!b || t1 <= t2)) { *t_out = t1; if (normal) *normal = n1; }
  else { *t_out = t2; if (normal) *normal = n2; }
  return true;
}
bool world_visible(v3 a, v3 b) {
  v3 d = v3_sub(b, a);
  float l = v3_len(d), t;
  if (l < 1e-4f) return true;
  return !world_raycast(a, v3_scale(d, 1 / l), l - 0.05f, &t, NULL);
}
static float cam_clear(v3 from, v3 to, float r);
float world_cam_clear(v3 from, v3 to, float r) { cam_probe = true; float c = cam_clear(from, to, r); cam_probe = false; return c; }
float world_cam_clear_all(v3 from, v3 to, float r) { return cam_clear(from, to, r); }
static float cam_clear(v3 from, v3 to, float r) {
  v3 d = v3_sub(to, from); float len = v3_len(d);
  if (len < 1e-4f) return 0;
  d = v3_scale(d, 1 / len);
  v3 rt = v3_cross(d, v3_(0, 1, 0));
  rt = v3_len(rt) < 1e-3f ? v3_(1, 0, 0) : v3_norm(rt);
  v3 up = v3_cross(rt, d);
  /* the centre and four points on the rim: a ray alone lets the near plane's corners into a wall */
  const v3 off[5] = { v3_(0, 0, 0), v3_scale(rt, r), v3_scale(rt, -r), v3_scale(up, r), v3_scale(up, -r) };
  float best = len;
  for (int i = 0; i < 5; i++) {
    float t; v3 n;
    if (world_raycast(v3_add(from, off[i]), d, best + r, &t, &n)) best = fminf(best, t - (i ? r * .5f : r));
  }
  best = fmaxf(0, best);
  /* a sphere test in the lab for edges the rays straddle: step back toward from until it is clear */
  if (stage == STAGE_TOWN) for (int k = 0; k < 8 && best > 0; k++) {
    v3 c = v3_add(from, v3_scale(d, best));
    if (!lab_push_sphere(&c, r, NULL)) break;
    best = fmaxf(0, best - r * .5f);
  }
  return best;
}
static float lab_floor(v3 p, float up) {
  if (!tris || !near_lab(p, 0)) return -1e9f;
  float t; v3 n, o = v3_(p.x, p.y + up, p.z);
  if (lab_raycast(o, v3_(0, -1, 0), 12, &t, &n)) return o.y - t;
  return -1e9f;
}
static inline bool over_lab(v3 p) { return p.x > LAB_X0 - .2f && p.x < LAB_X1 + .2f && p.z < .6f && p.z > -40.7f; }
float world_floor_y(v3 p) {
  if (stage != STAGE_TOWN) return 0;
  float best = over_lab(p) ? lab_floor(p, .01f) : -1e9f;
  if (best > -1e8f) return best;
  return city_floor_y(p.x, p.z, p.y - WORLD_STEP);
}
/* the highest walkable surface under a disc around p, no higher than p.y + up */
static float ground_under(v3 p, float r, float up) {
  static const float OFF[5][2] = {{0, 0}, {.6f, 0}, {-.6f, 0}, {0, .6f}, {0, -.6f}};
  float best = -1e9f;
  for (int i = 0; i < 5; i++) {
    float x = p.x + OFF[i][0] * r, z = p.z + OFF[i][1] * r;
    v3 q = v3_(x, p.y, z);
    float g = -1e9f;
    if (over_lab(q)) { float t; v3 n, o = v3_(x, p.y + up, z); if (lab_raycast(o, v3_(0, -1, 0), 12, &t, &n) && n.y > .55f) g = o.y - t; }
    if (g < -1e8f) g = city_floor_y(x, z, p.y + up - WORLD_STEP);
    best = fmaxf(best, g);
  }
  return best;
}
/* the closed front door blocks the doorway */
static void door_push(v3 *p, float r, v3 *n, bool *hit) {
  /* only a door that is shut and staying shut blocks: one that has begun to open lets people through,
   * so arriving at a run never bumps a half open leaf */
  if (door_open > .12f || door_target > .5f) return;
  float z = -LAB_DOOR_X;
  if (fabsf(p->x) < .9f && fabsf(p->z - z) < r + .04f) {
    float s = p->z > z ? 1 : -1;
    p->z = z + s * (r + .04f); *n = v3_(0, 0, s); *hit = true;
  }
}
void world_move(v3 *p, v3 *vel, float r, float h, float dt, bool *grounded) {
  bool was = *grounded;
  /* someone moving at the front door, or standing in it, calls it open: where they are, and where they will
   * be in .8 s. A body with no velocity at all is a path probe (story.c's nav grid), which never calls it */
  if (stage == STAGE_TOWN && door_auto && (vel->x != 0 || vel->y != 0 || vel->z != 0)) {
    float dz = -LAB_DOOR_X;
    float near = hypotf(p->x, p->z - dz), ahead = hypotf(p->x + vel->x * .8f, p->z + vel->z * .8f - dz);
    if (near < 1.6f || (ahead < 1.8f && near < 6.f)) door_call = .35f;
  }
  *grounded = false;
  v3 step = v3_scale(*vel, dt);
  float len = v3_len(step);
  int n = (int)ceilf(len / (r * .5f));
  if (n < 1) n = 1;
  if (n > 24) n = 24;
  v3 s = v3_scale(step, 1.f / n);
  for (int k = 0; k < n; k++) {
    p->x += s.x; p->z += s.z; p->y += s.y;
    v3 nn = v3_(0, 1, 0); bool hit = false;
    if (stage != STAGE_TOWN) kharena_push(stage, p, r, h, &nn, &hit);
    else {
      /* the body in the lab: spheres from just above a step to the head */
      float ys[3] = {WORLD_STEP + r, h * .5f, h - r};
      for (int it = 0; it < 2; it++) for (int j = 0; j < 3; j++) {
        if (ys[j] < r) continue;
        v3 c = v3_(p->x, p->y + ys[j], p->z), cn;
        if (lab_push_sphere(&c, r, &cn)) {
          p->x = c.x; p->z = c.z;
          if (j == 2 && cn.y < -.5f && vel->y > 0) { vel->y = 0; s.y = fminf(s.y, 0); }
          if (fabsf(cn.y) < .7f) { nn = v3_norm(v3_(cn.x, 0, cn.z)); hit = true; }
        }
      }
      city_push(p, r, h, &nn, &hit);
      door_push(p, r, &nn, &hit);
      /* the edge of the map */
      p->x = ds_clamp(p->x, CITY_X0, CITY_X1); p->z = ds_clamp(p->z, CITY_Z0, CITY_Z1);
    }
    if (hit) { float into = vel->x * nn.x + vel->z * nn.z; if (into < 0) { vel->x -= nn.x * into; vel->z -= nn.z * into; } }
    /* the floor: land, or step up onto anything below WORLD_STEP */
    float g = stage != STAGE_TOWN ? 0 : ground_under(*p, r * .8f, WORLD_STEP + .05f);
    float reach = (was || *grounded) ? WORLD_STEP : fmaxf(.05f, -s.y + .05f);
    if (g > -1e8f && vel->y <= .01f && p->y <= g + reach) { p->y = g; vel->y = 0; *grounded = true; }
    else if (g > -1e8f && p->y < g - .5f) { p->y = g; vel->y = 0; *grounded = true; }
  }
}

/* ── plan and places ── */
v3 world_lab(float plan_x, float plan_z) {
  return v3_(plan_z - LAB_DOOR_Z, 0, -plan_x);
}
float world_lab_yaw(float plan_deg) {
  /* plan yaw 0 faces plan +z = world +x; 90 faces plan +x = world -z. World yaw 0 faces +z, pi/2 faces +x. */
  return DS_PI * .5f + plan_deg * DS_PI / 180.f;
}
bool world_place(const char *name, v3 *pos, float *yaw) { return city_place(name, pos, yaw); }
bool world_seat(int i, v3 *pos, float *yaw, bool *soft) {
  if (i < 0 || i >= LAB_SEAT_N) return false;
  if (pos) *pos = v3_(LAB_SEATS[i].x, LAB_SEATS[i].top, LAB_SEATS[i].z);
  if (yaw) *yaw = LAB_SEATS[i].yaw;
  if (soft) *soft = LAB_SEATS[i].soft != 0;
  return true;
}
void world_door(float open) { door_auto = false; door_target = ds_clamp(open, 0, 1); }
void world_door_auto(bool on) { door_auto = on; }
float world_debug_door(void) { return door_open; }   /* for the world test */
void world_arena_phase(int phase) { kharena_set_phase(phase); }
void world_traffic(bool on) { city_traffic(on); }
bool world_train_bell(v3 *at) { return stage == STAGE_TOWN && city_train_bell(at); }

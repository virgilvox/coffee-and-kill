/* weapons.c: the rack (see weapons.h). The page's four builders (VT qT KT _T) as they were
 * in fighter.c; KEYHOLDER's five come from src/kh/kweapon.c. */
#include "weapons.h"
#include "special.h"
#include "geom.h"
#include "ck.h"
#include "kh/kweapon.h"
#include <string.h>
#include <math.h>

/* ns(color, metalness .8, roughness .4): MeshStandardMaterial. The builders'
 * materials are listed and released once the weapon is built (the meshes keep
 * what they use). */
static t3_material *made[64]; static int nmade;
static t3_material *keep(t3_material *m) { if (nmade < 64) made[nmade++] = m; return m; }
static void release_made(void) { for (int i = 0; i < nmade; i++) t3_release(made[i]); nmade = 0; }
static t3_material *ns(uint32_t c, float m, float r) {
  t3_material *mt = t3_mesh_standard_material_new(c);
  mt->metalness = m; mt->roughness = r;
  return keep(mt);
}
static t3_material *ha(void) { return ns(11844806, 0.95f, 0.28f); }
static t3_material *eo(void) { return ns(3883080, 0.9f, 0.4f); }
static t3_material *to(uint32_t c) { return ns(c, 0.25f, 0.55f); }
/* ge(geometry, material, x, y, z): a shadow casting mesh */
static t3_mesh *ge(t3_object *parent, t3_geometry *g, t3_material *m, float x, float y, float z) {
  t3_mesh *me = t3_mesh_new(g, m);
  t3_object_set_position(me, x, y, z);
  ((t3_object *)me)->cast_shadow = true;
  t3_object_add(parent, me);
  t3_release(me); t3_release(g);
  return me;
}
static t3_geometry *cyl(float rt, float rb, float h, int rs) { return t3_cylinder_geometry_new(rt, rb, h, rs, 1, false); }
static t3_geometry *box(float w, float h, float d) { return t3_box_geometry_new(w, h, d, 1, 1, 1); }

static t3_object *build_pipe(void) {   /* VT */
  t3_object *s = t3_object_new();
  t3_material *A = to(13116955), *P = ha();
  ge(s, cyl(0.019f, 0.026f, 0.58f, 16), A, 0, 0.11f, 0);
  ge(s, cyl(0.028f, 0.028f, 0.04f, 16), eo(), 0, -0.19f, 0);
  ge(s, box(0.075f, 0.2f, 0.046f), A, 0, 0.43f, 0);
  ge(s, box(0.115f, 0.05f, 0.046f), A, 0.02f, 0.545f, 0);
  ge(s, box(0.03f, 0.06f, 0.046f), A, 0.065f, 0.51f, 0);
  ge(s, box(0.032f, 0.13f, 0.04f), P, -0.04f, 0.47f, 0);
  for (int t = 0; t < 5; t++) ge(s, box(0.012f, 8e-3f, 0.04f), P, -0.016f, 0.42f + t * 0.025f, 0);
  t3_mesh *e = ge(s, cyl(0.04f, 0.04f, 0.034f, 20), P, -5e-3f, 0.33f, 0);
  t3_object_set_rotation(e, CK_PI / 2, 0, 0);
  for (int t = 0; t < 14; t++) {
    float D = t / 14.f * CK_PI * 2;
    ge(s, box(6e-3f, 6e-3f, 0.036f), eo(), -5e-3f + cosf(D) * 0.04f, 0.33f + sinf(D) * 0.04f, 0);
  }
  return s;
}
static t3_object *build_spanner(void) {   /* qT */
  t3_object *s = t3_object_new();
  static const float A[14][2] = {{-0.013f, -0.36f}, {0.013f, -0.36f}, {0.019f, -0.05f}, {0.05f, 0}, {0.05f, 0.07f}, {0.027f, 0.105f},
    {0.017f, 0.105f}, {0.017f, 0.05f}, {-0.017f, 0.05f}, {-0.017f, 0.105f}, {-0.027f, 0.105f}, {-0.05f, 0.07f}, {-0.05f, 0}, {-0.019f, -0.05f}};
  t3_geometry *e = geom_extrude(&A[0][0], 14, 0.018f, true, 4e-3f, 4e-3f, 2);
  t3_geometry_translate(e, 0, 0.19f, -9e-3f);
  t3_geometry_scale(e, 1.55f, 1.55f, 1.55f);
  ge(s, e, ns(14147044, 0.85f, 0.22f), 0, 0, 0);
  ge(s, t3_torus_geometry_new(0.018f, 6e-3f, 8, 16, CK_PI * 2), ns(14147044, 1, 0.2f), 0, -0.27f, 0);
  return s;
}
static t3_object *build_sledge(void) {   /* KT */
  t3_object *s = t3_object_new();
  ge(s, cyl(0.021f, 0.026f, 0.98f, 14), ns(11105594, 0.05f, 0.8f), 0, 0.3f, 0);
  ge(s, cyl(0.028f, 0.028f, 0.34f, 14), ns(1907997, 0.1f, 0.9f), 0, -0.02f, 0);
  ge(s, box(0.3f, 0.12f, 0.12f), eo(), 0, 0.72f, 0);
  for (int e = -1; e <= 1; e += 2) {
    t3_mesh *t = ge(s, cyl(0.066f, 0.066f, 0.03f, 18), ha(), e * 0.16f, 0.72f, 0);
    t3_object_set_rotation(t, 0, 0, CK_PI / 2);
  }
  ge(s, box(0.06f, 0.13f, 0.13f), to(15903518), 0, 0.72f, 0);
  return s;
}
static t3_object *build_crowbar(void) {   /* _T */
  t3_object *s = t3_object_new();
  t3_vec3 A[18]; int n = 0;
  A[n++] = t3_v3(0, -0.22f, 0); A[n++] = t3_v3(0, 0, 0); A[n++] = t3_v3(0, 0.46f, 0);
  float P = 0.075f, e = P, t = 0.6f;
  for (int g = 0; g <= 14; g++) {
    float i = CK_PI - g / 14.f * CK_PI * 0.85f;
    A[n++] = t3_v3(e + cosf(i) * P, t + sinf(i) * P * 1.3f, 0);
  }
  ge(s, geom_tube(A, n, false, 0, 0.2f, 48, 0.017f, 10, false), ns(3093563, 0.85f, 0.45f), 0, 0, 0);
  ge(s, cyl(0.019f, 0.019f, 0.18f, 10), to(15903518), 0, 0.18f, 0);
  ge(s, box(0.05f, 0.07f, 0.012f), ns(3093563, 0.85f, 0.45f), 0, -0.24f, 0);
  return s;
}
static t3_object *page_build(int w) {
  t3_object *o = w == 0 ? build_pipe() : w == 1 ? build_spanner() : w == 2 ? build_sledge() : build_crowbar();
  release_made();
  return o;
}

/*  key        name              tag         desc                                                    dmg    spd    trail     heavy  seg0    len    r      hands special          web    build        kh */
const weapon_def WEAPONS[] = {
  {"pipe", "PIPE WRENCH", "BALANCED", "Red steel. Solid on every count.", 1, 1, 16730682, false, -0.05f, 0.52f, 0.14f, 1, SP_EXTREME, true, page_build, 0},
  {"spanner", "BIG SPANNER", "FAST", "Chrome combination wrench. Quick hands.", 0.82f, 1.22f, 13625343, false, -0.05f, 0.36f, 0.12f, 1, SP_EXTREME, true, page_build, 1},
  {"sledge", "SLEDGE", "HEAVY", "Slow swing. Big dents.", 1.5f, 0.8f, 16756768, true, -0.05f, 0.74f, 0.17f, 1, SP_EXTREME, true, page_build, 2},
  {"crowbar", "CROWBAR", "REACH", "Long and mean. Hooks in from range.", 1.1f, 0.95f, 15903518, false, -0.05f, 0.62f, 0.13f, 1, SP_EXTREME, true, page_build, 3},
  /* KEYHOLDER: the blade segments are its SEG table, cut down to what lands in a duel this close */
  {"arc", "ARC BLADE", "PLASMA", "A welding handle with a blade of plasma. Extreme: Final Weld.", 0.95f, 1.12f, 0x58c8ff, false, 0.26f, 1.05f, 0.12f, 1, SP_FINAL_WELD, false, kweapon_build, WP_ARC},
  {"maul", "BALL PEEN MAUL", "GUARD BREAK", "A great hammer with a fantasy head. Extreme: Cold Forge Hammerfall.", 1.55f, 0.78f, 0xff9030, true, 0.8f, 1.16f, 0.2f, 1, SP_HAMMERFALL, false, kweapon_build, WP_MAUL},
  {"twin", "DEBURR TWINS", "FASTEST", "Paired curved daggers. Long combos. Extreme: Thousand Burr Cut.", 0.68f, 1.42f, 0x40f0d0, false, 0.05f, 0.5f, 0.12f, 2, SP_BURR_CUT, false, kweapon_build, WP_TWIN},
  {"spear", "SPOOL NAGINATA", "LONG REACH", "A glaive wound from printer filament. Extreme: First Layer Adhesion.", 1.08f, 0.92f, 0xff8a24, false, 0.7f, 1.3f, 0.12f, 1, SP_ADHESION, false, kweapon_build, WP_SPEAR},
  {"staff", "BEAM STAFF", "LASER", "A laser tube on a staff. Extreme: Full Power, No Air Assist.", 0.9f, 1.15f, 0xff3a78, false, 0.6f, 1.25f, 0.13f, 1, SP_FULL_POWER, false, kweapon_build, WP_STAFF},
};
const int WEAPON_N = (int)(sizeof WEAPONS / sizeof WEAPONS[0]);

int weapon_index(const char *k) { for (int i = 0; k && i < WEAPON_N; i++) if (!strcmp(WEAPONS[i].key, k)) return i; return -1; }
t3_object *weapon_build(int w) { const weapon_def *d = &WEAPONS[w < 0 || w >= WEAPON_N ? 0 : w]; return d->build(d->kh); }
int weapon_web_count(void) { int n = 0; for (int i = 0; i < WEAPON_N; i++) n += WEAPONS[i].web; return n; }

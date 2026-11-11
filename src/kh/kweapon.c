/* kweapon.c: KEYHOLDER's five weapon props (its src/weapon.c builders, unchanged), for
 * weapons.c. Each weapon is flat shaded triangles with vertex colours in
 * up to three merged meshes (lit body, opaque glow, additive halo), so three draw calls
 * however ornate. Weapon frame: the fist at the origin, +y along the blade, +z the cutting
 * edge, x the flat. */
#include "kweapon.h"
#include <stdlib.h>
#include <string.h>

typedef struct { float r, g, b; } c3;
static c3 hex(uint32_t h, float k) { t3_color c = t3_color_hex(h); c3 o = { c.r * k, c.g * k, c.b * k }; return o; }

/* ---- the mesh builder ---- */
typedef struct { float *pos, *nor, *col; int n, cap; } mb;
static mb *cur;
static v3 o_, ax_, ay_, az_;
static void xf_reset(void) { o_ = v3_(0, 0, 0); ax_ = v3_(1, 0, 0); ay_ = v3_(0, 1, 0); az_ = v3_(0, 0, 1); }
static v3 xf(v3 p) { return v3_add(o_, v3_add(v3_scale(ax_, p.x), v3_add(v3_scale(ay_, p.y), v3_scale(az_, p.z)))); }
/* place the next parts: origin, and local +y pointing along dir, local +z near hint */
static void xf_set(v3 o, v3 dir, v3 hint) {
  o_ = o; ay_ = v3_norm(dir);
  ax_ = v3_norm(v3_cross(ay_, hint)); az_ = v3_cross(ax_, ay_);
}
static void tri_c(v3 a, v3 b, v3 c, c3 ca, c3 cb, c3 cc) {
  if (cur->n + 3 > cur->cap) {
    cur->cap = cur->cap ? cur->cap * 2 : 512;
    cur->pos = realloc(cur->pos, sizeof(float) * 3 * cur->cap);
    cur->nor = realloc(cur->nor, sizeof(float) * 3 * cur->cap);
    cur->col = realloc(cur->col, sizeof(float) * 3 * cur->cap);
  }
  a = xf(a); b = xf(b); c = xf(c);
  v3 nn = v3_norm(v3_cross(v3_sub(b, a), v3_sub(c, a)));
  v3 P[3] = {a, b, c}; c3 C[3] = {ca, cb, cc};
  for (int i = 0; i < 3; i++) {
    int k = cur->n++;
    cur->pos[k*3] = P[i].x; cur->pos[k*3+1] = P[i].y; cur->pos[k*3+2] = P[i].z;
    cur->nor[k*3] = nn.x; cur->nor[k*3+1] = nn.y; cur->nor[k*3+2] = nn.z;
    cur->col[k*3] = C[i].r; cur->col[k*3+1] = C[i].g; cur->col[k*3+2] = C[i].b;
  }
}
static void tri(v3 a, v3 b, v3 c, c3 col) { tri_c(a, b, c, col, col, col); }
static void quad(v3 a, v3 b, v3 c, v3 d, c3 col) { tri(a, b, c, col); tri(a, c, d, col); }

/* a lofted tube along local y: stations of y, radius x, radius z, centre offset z, colour */
typedef struct { float y, rx, rz, oz; c3 c; } st_t;
static void loft(const st_t *s, int n, int sides, bool caps) {
  for (int i = 0; i + 1 < n; i++) for (int k = 0; k < sides; k++) {
    float a0 = k * 6.28318f / sides, a1 = (k + 1) * 6.28318f / sides;
    v3 p00 = v3_(cosf(a0) * s[i].rx, s[i].y, s[i].oz + sinf(a0) * s[i].rz), p01 = v3_(cosf(a1) * s[i].rx, s[i].y, s[i].oz + sinf(a1) * s[i].rz);
    v3 p10 = v3_(cosf(a0) * s[i+1].rx, s[i+1].y, s[i+1].oz + sinf(a0) * s[i+1].rz), p11 = v3_(cosf(a1) * s[i+1].rx, s[i+1].y, s[i+1].oz + sinf(a1) * s[i+1].rz);
    tri(p00, p10, p11, s[i].c); tri(p00, p11, p01, s[i].c);
  }
  if (caps) for (int k = 0; k < sides; k++) {
    float a0 = k * 6.28318f / sides, a1 = (k + 1) * 6.28318f / sides;
    const st_t *b = &s[0], *t = &s[n-1];
    tri(v3_(0, b->y, b->oz), v3_(cosf(a0)*b->rx, b->y, b->oz + sinf(a0)*b->rz), v3_(cosf(a1)*b->rx, b->y, b->oz + sinf(a1)*b->rz), b->c);
    tri(v3_(0, t->y, t->oz), v3_(cosf(a1)*t->rx, t->y, t->oz + sinf(a1)*t->rz), v3_(cosf(a0)*t->rx, t->y, t->oz + sinf(a0)*t->rz), t->c);
  }
}
static void cyl(float y0, float y1, float r0, float r1, int sides, c3 c) {
  st_t s[2] = { {y0, r0, r0, 0, c}, {y1, r1, r1, 0, c} };
  loft(s, 2, sides, true);
}
static void ball(float y, float r, int sides, c3 c) {
  enum { R = 7 };
  st_t s[R];
  for (int i = 0; i < R; i++) { float a = -1.5708f + 3.14159f * i / (R - 1); s[i] = (st_t){ y + sinf(a) * r, fmaxf(cosf(a) * r, .0005f), fmaxf(cosf(a) * r, .0005f), 0, c }; }
  loft(s, R, sides, false);
}
static void box(float cx, float cy, float cz, float w, float h, float d, c3 c) {
  float x0 = cx - w/2, x1 = cx + w/2, y0 = cy - h/2, y1 = cy + h/2, z0 = cz - d/2, z1 = cz + d/2;
  quad(v3_(x0,y0,z1), v3_(x1,y0,z1), v3_(x1,y1,z1), v3_(x0,y1,z1), c);
  quad(v3_(x1,y0,z0), v3_(x0,y0,z0), v3_(x0,y1,z0), v3_(x1,y1,z0), c);
  quad(v3_(x1,y0,z1), v3_(x1,y0,z0), v3_(x1,y1,z0), v3_(x1,y1,z1), c);
  quad(v3_(x0,y0,z0), v3_(x0,y0,z1), v3_(x0,y1,z1), v3_(x0,y1,z0), c);
  quad(v3_(x0,y1,z1), v3_(x1,y1,z1), v3_(x1,y1,z0), v3_(x0,y1,z0), c);
  quad(v3_(x0,y0,z0), v3_(x1,y0,z0), v3_(x1,y0,z1), v3_(x0,y0,z1), c);
}
/* a flat blade in the yz plane with a ridge down its middle: stations of y, front edge z, back edge z, half thickness */
typedef struct { float y, zf, zb, t; } bl_t;
static void blade(const bl_t *s, int n, c3 edge, c3 face, c3 spine) {
  for (int i = 0; i + 1 < n; i++) {
    const bl_t *a = &s[i], *b = &s[i+1];
    float mza = (a->zf + a->zb) * .5f, mzb = (b->zf + b->zb) * .5f;
    v3 fa = v3_(0, a->y, a->zf), fb = v3_(0, b->y, b->zf), ba = v3_(0, a->y, a->zb), bb = v3_(0, b->y, b->zb);
    for (int side = -1; side <= 1; side += 2) {
      v3 ra = v3_(side * a->t, a->y, mza), rb = v3_(side * b->t, b->y, mzb);
      if (side > 0) { tri_c(fa, ra, rb, edge, face, face); tri_c(fa, rb, fb, edge, face, edge); tri_c(ra, ba, bb, face, spine, spine); tri_c(ra, bb, rb, face, spine, face); }
      else { tri_c(fa, rb, ra, edge, face, face); tri_c(fa, fb, rb, edge, edge, face); tri_c(ra, bb, ba, face, spine, spine); tri_c(ra, rb, bb, face, face, spine); }
    }
  }
}
/* a torus about local y at height y */
static void torus(float y, float R, float r, int segs, int sides, c3 c) {
  for (int i = 0; i < segs; i++) for (int k = 0; k < sides; k++) {
    float a0 = i * 6.28318f / segs, a1 = (i + 1) * 6.28318f / segs, b0 = k * 6.28318f / sides, b1 = (k + 1) * 6.28318f / sides;
#define TP(A, B) v3_(cosf(A) * (R + r * cosf(B)), y + r * sinf(B), sinf(A) * (R + r * cosf(B)))
    quad(TP(a0, b0), TP(a0, b1), TP(a1, b1), TP(a1, b0), c);
#undef TP
  }
}

/* ---- the five ---- */
enum { M_BODY, M_GLOW, M_HALO, M_N };
static mb B[M_N];
static void use(int m) { cur = &B[m]; }

/* the Arc Blade: a welding torch handle, ceramic nozzle, a long curved blade of plasma */
static void build_arc(void) {
  c3 rubber = hex(0x23252c, 1), copper = hex(0xc8743c, 1), brass = hex(0xd9a441, 1), ceramic = hex(0xd8cfc0, 1), steel = hex(0x8a92a0, 1);
  use(M_BODY);
  st_t grip[] = { {-.14f,.024f,.024f,0,steel}, {-.12f,.027f,.027f,0,rubber}, {-.06f,.024f,.024f,0,rubber}, {-.05f,.028f,.028f,0,rubber}, {-.01f,.024f,.024f,0,rubber},
                  {0,.028f,.028f,0,rubber}, {.04f,.024f,.024f,0,rubber}, {.05f,.028f,.028f,0,rubber}, {.11f,.03f,.03f,0,rubber} };
  loft(grip, 9, 8, true);
  cyl(.11f, .135f, .037f, .037f, 8, copper);                 /* the collar */
  /* the crossguard: a welding clamp, swept forward */
  quad(v3_(-.018f,.135f,-.075f), v3_(.018f,.135f,-.075f), v3_(.018f,.155f,.095f), v3_(-.018f,.155f,.095f), brass);
  quad(v3_(.018f,.115f,-.06f), v3_(-.018f,.115f,-.06f), v3_(-.018f,.135f,.08f), v3_(.018f,.135f,.08f), brass);
  box(0, .13f, -.07f, .036f, .05f, .03f, copper); box(0, .14f, .09f, .036f, .05f, .03f, copper);
  cyl(.135f, .195f, .03f, .021f, 8, ceramic);                /* the nozzle */
  cyl(.195f, .21f, .023f, .018f, 8, copper);
  /* the blade: a white core, a cyan body, a soft halo; it starts narrow at the nozzle and swells like a katana */
  bl_t core[] = { {.2f,.008f,-.008f,.004f}, {.3f,.02f,-.016f,.008f}, {.7f,.026f,-.01f,.009f}, {1.05f,.035f,.012f,.008f}, {1.2f,.052f,.04f,.003f}, {1.26f,.075f,.074f,.0005f} };
  use(M_GLOW); blade(core, 6, hex(0xffffff, 3), hex(0xd8f8ff, 2.2f), hex(0x9fe8ff, 1.8f));
  bl_t body[] = { {.19f,.016f,-.016f,.012f}, {.3f,.04f,-.03f,.02f}, {.7f,.046f,-.026f,.022f}, {1.05f,.058f,-.008f,.02f}, {1.2f,.07f,.024f,.012f}, {1.3f,.085f,.082f,.001f} };
  use(M_HALO); blade(body, 6, hex(0x58c8ff, 1.3f), hex(0x2a90f0, .9f), hex(0x1a60e0, .6f));
  bl_t outer[] = { {.18f,.03f,-.03f,.03f}, {.3f,.075f,-.06f,.045f}, {.7f,.08f,-.055f,.05f}, {1.05f,.095f,-.03f,.045f}, {1.22f,.1f,.012f,.03f}, {1.36f,.09f,.086f,.001f} };
  blade(outer, 6, hex(0x1c60c0, .5f), hex(0x123c90, .32f), hex(0x0c2868, .15f));
  /* arc rings at the root */
  torus(.235f, .05f, .006f, 10, 3, hex(0x8fe0ff, 1.2f));
  torus(.3f, .043f, .004f, 10, 3, hex(0x5fb0ff, .9f));
}

/* the Ball Peen Maul: a long ringed haft, a huge forged head: flat face forward, peen behind, crowned and winged */
static void build_maul(void) {
  c3 wood = hex(0x4a2c18, 1), dark = hex(0x2a1a10, 1), iron = hex(0x4e525e, 1), ironl = hex(0x7a808e, 1), gold = hex(0xe0ac3c, 1), goldd = hex(0x9a6c1e, 1);
  use(M_BODY);
  st_t haft[] = { {-.34f,.02f,.02f,0,gold}, {-.3f,.032f,.032f,0,gold}, {-.27f,.026f,.026f,0,goldd}, {-.26f,.024f,.024f,0,dark}, {.1f,.024f,.024f,0,dark},
                  {.11f,.03f,.03f,0,gold}, {.14f,.03f,.03f,0,gold}, {.15f,.023f,.023f,0,wood}, {.55f,.026f,.026f,0,wood}, {.56f,.033f,.033f,0,gold}, {.59f,.033f,.033f,0,gold}, {.6f,.027f,.027f,0,wood}, {.9f,.03f,.03f,0,wood} };
  loft(haft, 13, 8, true);
  for (int i = 0; i < 6; i++) { float y = -.22f + i * .055f; torus(y, .026f, .004f, 8, 3, hex(0x6a4a30, 1)); }
  /* langets up the haft */
  box(.03f, .8f, 0, .012f, .3f, .05f, goldd); box(-.03f, .8f, 0, .012f, .3f, .05f, goldd);
  /* the head along z: its own frame has local y along z */
  xf_set(v3_(0, 1.04f, 0), v3_(0, 0, 1), v3_(0, 1, 0));
  st_t head[] = { {-.3f,.11f,.11f,0,ironl}, {-.27f,.14f,.14f,0,iron}, {-.2f,.13f,.13f,0,iron}, {-.14f,.17f,.17f,0,goldd}, {-.1f,.17f,.17f,0,gold}, {-.06f,.135f,.135f,0,iron},
                  {.08f,.135f,.135f,0,iron}, {.12f,.17f,.17f,0,gold}, {.17f,.17f,.17f,0,goldd}, {.22f,.12f,.12f,0,iron}, {.3f,.1f,.1f,0,iron} };
  loft(head, 11, 8, false);
  /* the striking face, on +z, a wide flared disc */
  st_t face[] = { {.3f,.1f,.1f,0,iron}, {.36f,.13f,.13f,0,iron}, {.44f,.17f,.17f,0,ironl}, {.46f,.17f,.17f,0,hex(0xb8bcc8,1)}, {.47f,.0005f,.0005f,0,hex(0xc8ccd8,1)} };
  loft(face, 5, 10, false);
  /* the peen, on -z, a great ball */
  ball(-.42f, .16f, 10, ironl);
  torus(-.3f, .125f, .02f, 12, 4, gold);
  xf_reset();
  /* the crown above, the wings each side */
  bl_t crown[] = { {1.16f,.06f,-.06f,.03f}, {1.3f,.03f,-.03f,.016f}, {1.46f,.002f,-.002f,.002f} };
  use(M_BODY); blade(crown, 3, gold, goldd, goldd);
  for (int s = -1; s <= 1; s += 2) {
    xf_set(v3_(s * .12f, 1.04f, .02f), v3_((float)s, .25f, 0), v3_(0, 0, 1));
    bl_t wing[] = { {0,.11f,-.13f,.018f}, {.1f,.14f,-.1f,.014f}, {.2f,.1f,-.03f,.008f}, {.3f,.07f,.05f,.001f} };
    blade(wing, 4, gold, iron, goldd);
  }
  xf_reset();
  /* a spike below the head */
  xf_set(v3_(0, .9f, 0), v3_(0, -1, 0), v3_(0, 0, 1));
  cyl(0, .08f, .05f, .027f, 8, goldd);
  xf_reset();
  /* forge light: slits round the head and an ember face in the wings */
  use(M_GLOW);
  xf_set(v3_(0, 1.04f, 0), v3_(0, 0, 1), v3_(0, 1, 0));
  for (int i = 0; i < 8; i++) {
    float a = i * .7854f + .39f;
    v3 n = v3_(cosf(a), 0, sinf(a));
    v3 p = v3_scale(n, .137f), t = v3_(-sinf(a), 0, cosf(a));
    v3 l0 = v3_add(p, v3_scale(t, .035f)), l1 = v3_sub(p, v3_scale(t, .035f));
    quad(v3_(l1.x, -.05f, l1.z), v3_(l0.x, -.05f, l0.z), v3_(l0.x, .07f, l0.z), v3_(l1.x, .07f, l1.z), hex(0xffa040, 2.2f));
  }
  torus(.47f, .13f, .008f, 14, 3, hex(0xffc060, 2.4f));
  xf_reset();
  use(M_HALO);
  xf_set(v3_(0, 1.04f, 0), v3_(0, 0, 1), v3_(0, 1, 0));
  st_t hal[] = { {-.1f,.2f,.2f,0,hex(0xc04808,.45f)}, {.14f,.2f,.2f,0,hex(0xc04808,.45f)} };
  loft(hal, 2, 10, false);
  xf_reset();
}

/* the Deburr Twins: a short hooked blade like the swivel of a deburring tool, teal edge fire */
static void build_twin(void) {
  c3 grip = hex(0x20303a, 1), band = hex(0x2ec4b6, 1), steel = hex(0xa8b4c0, 1), steeld = hex(0x505a68, 1);
  use(M_BODY);
  st_t g[] = { {-.09f,.016f,.022f,0,steeld}, {-.08f,.022f,.028f,0,grip}, {-.02f,.018f,.024f,0,grip}, {0,.022f,.028f,0,grip}, {.04f,.018f,.024f,0,grip}, {.075f,.024f,.03f,0,grip} };
  loft(g, 6, 8, true);
  torus(-.1f, .022f, .006f, 10, 3, band);                    /* the pommel ring */
  cyl(.075f, .1f, .03f, .024f, 8, band);
  bl_t b[] = { {.1f,.024f,-.024f,.009f}, {.2f,.052f,-.024f,.011f}, {.3f,.09f,-.002f,.009f}, {.38f,.14f,.044f,.007f}, {.44f,.2f,.108f,.004f}, {.47f,.27f,.2f,.001f} };
  blade(b, 6, steel, steeld, steel);
  /* the back spur */
  bl_t sp[] = { {.09f,-.018f,-.04f,.007f}, {.13f,-.03f,-.09f,.005f}, {.14f,-.09f,-.12f,.001f} };
  blade(sp, 3, steel, steeld, steel);
  bl_t ed[] = { {.11f,.04f,.018f,.004f}, {.2f,.066f,.044f,.005f}, {.3f,.104f,.08f,.005f}, {.38f,.156f,.13f,.004f}, {.44f,.214f,.19f,.003f}, {.48f,.285f,.27f,.0005f} };
  use(M_GLOW); blade(ed, 6, hex(0xc8fff4, 2.8f), hex(0x60ffe0, 2), hex(0x2ec4b6, 1.3f));
  bl_t ha[] = { {.1f,.075f,.0f,.02f}, {.2f,.098f,.02f,.024f}, {.3f,.135f,.05f,.024f}, {.38f,.19f,.1f,.02f}, {.45f,.25f,.16f,.012f}, {.5f,.31f,.28f,.001f} };
  use(M_HALO); blade(ha, 6, hex(0x2ee0c0, .7f), hex(0x12907c, .4f), hex(0x0c5a50, .15f));
}

/* the Spool Naginata: a shaft wound in printer filament, a spool for a collar, a big curved head */
static void build_spear(void) {
  c3 fa = hex(0xff7a1a, 1), fb = hex(0xfff0dc, 1), steel = hex(0xb0b8c4, 1), steeld = hex(0x58606e, 1), dark = hex(0x24262e, 1), spool = hex(0x2a2c34, 1);
  use(M_BODY);
  /* the winding: each facet's colour follows a helix */
  { enum { SIDES = 8 };
    float y0 = -.72f, y1 = .92f; int rings = 44;
    for (int i = 0; i < rings; i++) for (int k = 0; k < SIDES; k++) {
      float ya = y0 + (y1 - y0) * i / rings, yb = y0 + (y1 - y0) * (i + 1) / rings;
      float a0 = k * 6.28318f / SIDES, a1 = (k + 1) * 6.28318f / SIDES, r = .021f + .0016f * (i & 1);
      int band = (i + k * 2) % 5;
      c3 c = band < 2 ? fa : band < 4 ? fb : hex(0xc05010, 1);
      quad(v3_(cosf(a0)*r, ya, sinf(a0)*r), v3_(cosf(a0)*r, yb, sinf(a0)*r), v3_(cosf(a1)*r, yb, sinf(a1)*r), v3_(cosf(a1)*r, ya, sinf(a1)*r), c);
    } }
  st_t butt[] = { {-.86f,.008f,.008f,0,steel}, {-.8f,.024f,.024f,0,steel}, {-.74f,.028f,.028f,0,steeld}, {-.72f,.028f,.028f,0,dark} };
  loft(butt, 4, 8, true);
  cyl(.0f, .16f, .029f, .029f, 8, dark);                     /* the wrapped grip */
  /* the spool: two flanges and a hot orange wind between them */
  cyl(.86f, .875f, .105f, .105f, 12, spool);
  cyl(.96f, .975f, .105f, .105f, 12, spool);
  cyl(.875f, .96f, .082f, .082f, 12, fa);
  for (int i = 0; i < 5; i++) torus(.885f + i * .016f, .082f, .006f, 12, 3, i & 1 ? fb : hex(0xe06010, 1));
  cyl(.975f, 1.02f, .04f, .03f, 8, steeld);
  /* the head: wide and sweeping back like a glaive */
  bl_t h[] = { {1.0f,.03f,-.03f,.012f}, {1.08f,.078f,-.05f,.018f}, {1.2f,.13f,-.052f,.02f}, {1.34f,.17f,-.026f,.018f}, {1.48f,.2f,.03f,.014f}, {1.6f,.2f,.1f,.009f}, {1.7f,.18f,.15f,.004f}, {1.76f,.15f,.145f,.001f} };
  blade(h, 8, hex(0xe8eef4, 1), steeld, steeld);
  /* a tang spike on the back */
  bl_t sp[] = { {1.02f,-.03f,-.06f,.01f}, {1.08f,-.05f,-.15f,.007f}, {1.12f,-.13f,-.2f,.001f} };
  blade(sp, 3, steel, steeld, steel);
  bl_t ed[] = { {1.06f,.082f,.05f,.004f}, {1.2f,.142f,.11f,.005f}, {1.34f,.182f,.15f,.005f}, {1.48f,.21f,.18f,.004f}, {1.6f,.207f,.18f,.003f}, {1.7f,.186f,.168f,.002f}, {1.77f,.15f,.148f,.0005f} };
  use(M_GLOW); blade(ed, 7, hex(0xfff0c8, 2.6f), hex(0xffa040, 2), hex(0xff7a1a, 1.3f));
  torus(.92f, .094f, .004f, 12, 3, hex(0xffc070, 2));
  bl_t ha[] = { {1.05f,.12f,.02f,.02f}, {1.2f,.18f,.07f,.024f}, {1.34f,.22f,.11f,.024f}, {1.48f,.25f,.15f,.022f}, {1.6f,.24f,.16f,.016f}, {1.74f,.2f,.17f,.008f}, {1.82f,.15f,.145f,.001f} };
  use(M_HALO); blade(ha, 7, hex(0xff8020, .7f), hex(0xa04808, .35f), hex(0x602804, .1f));
}

/* the Beam Staff: a laser tube in brackets, a crystal in a broken halo */
static void build_staff(void) {
  c3 steel = hex(0xb4b8c6, 1), dark = hex(0x24283a, 1), gold = hex(0xd0b060, 1);
  use(M_BODY);
  st_t foot[] = { {-.82f,.006f,.006f,0,steel}, {-.76f,.03f,.03f,0,steel}, {-.7f,.036f,.036f,0,dark}, {-.68f,.03f,.03f,0,dark} };
  loft(foot, 4, 8, true);
  cyl(-.1f, .14f, .031f, .031f, 8, dark);                    /* the grip */
  for (int i = 0; i < 7; i++) {
    float y = -.66f + i * .275f;
    if (i == 2) continue;
    torus(y, .03f, .01f, 8, 3, i % 3 == 0 ? gold : steel);
  }
  /* the rails beside the tube */
  for (int i = 0; i < 3; i++) { float a = i * 2.094f; box(cosf(a) * .03f, .13f, sinf(a) * .03f, .007f, 1.62f, .007f, dark); }
  /* the head: a fork of three prongs round the crystal */
  cyl(.96f, 1.03f, .034f, .05f, 8, dark);
  for (int i = 0; i < 3; i++) {
    float a = i * 2.094f + .5f;
    xf_set(v3_(cosf(a) * .05f, 1.03f, sinf(a) * .05f), v3_(cosf(a) * .5f, 1, sinf(a) * .5f), v3_(-sinf(a), 0, cosf(a)));
    cyl(0, .13f, .012f, .012f, 4, gold);
    xf_set(v3_(cosf(a) * .115f, 1.145f, sinf(a) * .115f), v3_(-cosf(a) * .35f, 1, -sinf(a) * .35f), v3_(-sinf(a), 0, cosf(a)));
    cyl(0, .16f, .012f, .002f, 4, gold);
  }
  xf_reset();
  /* the tube's bright core */
  use(M_GLOW);
  cyl(-.68f, .96f, .013f, .013f, 6, hex(0xffd0e0, 2.6f));
  /* the crystal, a long double point */
  { st_t cr[] = { {1.06f,.002f,.002f,0,hex(0xff4080,1.5f)}, {1.16f,.052f,.052f,0,hex(0xff90b0,2.4f)}, {1.3f,.002f,.002f,0,hex(0xffffff,3)} };
    loft(cr, 3, 6, false); }
  use(M_HALO);
  cyl(-.68f, .96f, .025f, .025f, 8, hex(0xff2a60, .75f));
  cyl(-.68f, .96f, .04f, .04f, 8, hex(0xc01848, .3f));
  { st_t cr[] = { {1.03f,.002f,.002f,0,hex(0xff3070,.5f)}, {1.16f,.09f,.09f,0,hex(0xff3070,.5f)}, {1.35f,.002f,.002f,0,hex(0xff3070,.5f)} };
    loft(cr, 3, 8, false); }
  /* the halo, broken into three arcs */
  xf_set(v3_(0, 1.16f, 0), v3_(0, 0, 1), v3_(0, 1, 0));
  for (int k = 0; k < 3; k++) for (int i = 0; i < 6; i++) {
    float a0 = k * 2.094f + i * .25f, a1 = a0 + .25f, r = .17f, w = .011f;
    quad(v3_(cosf(a0)*(r-w), 0, sinf(a0)*(r-w)), v3_(cosf(a0)*(r+w), 0, sinf(a0)*(r+w)), v3_(cosf(a1)*(r+w), 0, sinf(a1)*(r+w)), v3_(cosf(a1)*(r-w), 0, sinf(a1)*(r-w)), hex(0xff5090, 1.5f));
  }
  xf_reset();
}

/* ---- the props ---- */
static t3_material *MAT[M_N];
static t3_geometry *GEO[WP_COUNT][M_N];
static const uint32_t COL[WP_COUNT] = { 0x58c8ff, 0xff9030, 0x40f0d0, 0xff8a24, 0xff3a78 };
/* the blade as seen by the trails and hit sparks, along +y */
static const float SEG[WP_COUNT][2] = { {.26f, 1.3f}, {.8f, 1.22f}, {.1f, .5f}, {1.02f, 1.72f}, {.75f, 1.3f} };
static const char *NAMES[WP_COUNT] = { "ARC BLADE", "BALL PEEN MAUL", "DEBURR TWINS", "SPOOL NAGINATA", "BEAM STAFF" };

static t3_geometry *geo_from(mb *m) {
  if (!m->n) return NULL;
  t3_geometry *g = t3_geometry_new();
  t3_attribute *p = t3_attribute_new(T3_FLOAT32, m->pos, m->n, 3), *n = t3_attribute_new(T3_FLOAT32, m->nor, m->n, 3), *c = t3_attribute_new(T3_FLOAT32, m->col, m->n, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, p); t3_release(p);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, n); t3_release(n);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, c); t3_release(c);
  free(m->pos); free(m->nor); free(m->col); memset(m, 0, sizeof *m);
  return g;
}
static void build_all(void) {
  if (GEO[0][0]) return;
  void (*F[WP_COUNT])(void) = { build_arc, build_maul, build_twin, build_spear, build_staff };
  for (int w = 0; w < WP_COUNT; w++) {
    xf_reset(); F[w]();
    if (w == WP_MAUL) for (int m = 0; m < M_N; m++) for (int i = 0; i < B[m].n; i++) {   /* the head a size down, about its middle */
      float *p = B[m].pos + i * 3;
      if (p[1] > .86f) { p[0] *= .8f; p[2] *= .8f; p[1] = 1.0f + (p[1] - 1.04f) * .8f; }
    }
    int tot = 0;
    for (int m = 0; m < M_N; m++) { tot += B[m].n / 3; GEO[w][m] = geo_from(&B[m]); }
    ds_log("weapon %s: %d tris", NAMES[w], tot);
  }
  MAT[M_BODY] = t3_mesh_lambert_material_new(0xffffff);
  MAT[M_BODY]->vertex_colors = true; MAT[M_BODY]->fog = true; MAT[M_BODY]->side = T3_DOUBLE_SIDE; MAT[M_BODY]->version++;
  MAT[M_GLOW] = t3_mesh_basic_material_new(0xffffff);
  MAT[M_GLOW]->vertex_colors = true; MAT[M_GLOW]->fog = false; MAT[M_GLOW]->side = T3_DOUBLE_SIDE; MAT[M_GLOW]->tone_mapped = false; MAT[M_GLOW]->version++;
  MAT[M_HALO] = t3_mesh_basic_material_new(0xffffff);
  MAT[M_HALO]->vertex_colors = true; MAT[M_HALO]->fog = false; MAT[M_HALO]->side = T3_DOUBLE_SIDE; MAT[M_HALO]->tone_mapped = false;
  MAT[M_HALO]->transparent = true; MAT[M_HALO]->blending = T3_ADDITIVE_BLENDING; MAT[M_HALO]->depth_write = false; MAT[M_HALO]->version++;
}

/* a held prop: up to three meshes sharing the built geometry */
t3_object *kweapon_build(int w) {
  build_all();
  t3_object *h = t3_object_new();
  if (w < 0 || w >= WP_COUNT) return h;
  for (int m = 0; m < M_N; m++) if (GEO[w][m]) {
    t3_mesh *me = t3_mesh_new(GEO[w][m], MAT[m]);
    t3_object *o = (t3_object *)me;
    o->frustum_culled = false; o->cast_shadow = m == M_BODY; o->receive_shadow = false;
    if (m == M_HALO) o->render_order = 3;
    t3_object_add(h, me);
    t3_release(me);
  }
  return h;
}
uint32_t kweapon_color(int w) { return w >= 0 && w < WP_COUNT ? COL[w] : 0xffffff; }
void kweapon_segment(int w, float *base, float *tip) { *base = SEG[w][0]; *tip = SEG[w][1]; }

/* arena.c: the hackerspace, Z0 of the page and everything it calls, in the
 * page's order: the painted canvas textures (ul floor, ll walls, dl pegboard,
 * LC hazard tape, bl floor decal, Ll sign, xl shutter, Nl light cone, pl dust,
 * Ul coffee neon, Tl the Arduino Mega), the room, the coffee bar, the desks
 * (H), the shelves (I) with their boxes, the Kenney props through T(), the
 * Mega on the wall (G0), the robot eye (k0), the screens (qr/jl), the
 * beacons (Kr), the cables (F0), the sparks (Y0), the hanging lamps, the
 * lights and the dust, with every updater.
 *
 * Lit and normal-blended things are three.c meshes. The additive ones (the
 * window light cones, the beacon beams, the sparks, the dust) draw in
 * arena_fx_draw through fx.h, as WebGL blends them. */
#include "arena.h"
#include "batch.h"
#include "ck.h"
#include "cv.h"
#include "fx.h"
#include "geom.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define RA ROOM_HX
#define FP ROOM_HZ
#define MP ROOM_HY
#define PI CK_PI

static t3_scene *S;      /* the scene (s) */
static t3_object *T;     /* the arena group (t) */
static t3_vec3 focus = {0, 1.2f, 0};

/* ---- small helpers ---- */

static void add(void *parent, void *child) { t3_object_add(T3_OBJ(parent), child); }

/* new le({color, roughness, metalness}): MeshStandardMaterial */
static t3_material *std_mat(uint32_t color, float rough, float metal) {
  t3_material *m = t3_mesh_standard_material_new(color);
  m->roughness = rough; m->metalness = metal;
  return m;
}
/* new jP({color, toneMapped: false}) */
static t3_material *basic_mat(uint32_t color, bool tone) {
  t3_material *m = t3_mesh_basic_material_new(color);
  m->tone_mapped = tone;
  return m;
}
static t3_mesh *mesh(void *parent, t3_geometry *g, t3_material *m, float x, float y, float z) {
  t3_mesh *o = t3_mesh_new(g, m);
  t3_object_set_position(o, x, y, z);
  if (parent) add(parent, o);
  t3_release(g); t3_release(m);
  return o;
}
/* a mesh sharing a material the caller keeps */
static t3_mesh *mesh_k(void *parent, t3_geometry *g, t3_material *m, float x, float y, float z) {
  t3_mesh *o = t3_mesh_new(g, m);
  t3_object_set_position(o, x, y, z);
  if (parent) add(parent, o);
  t3_release(g);
  return o;
}
static t3_geometry *box(float w, float h, float d) { return t3_box_geometry_new(w, h, d, 1, 1, 1); }
static t3_geometry *cyl(float rt, float rb, float h, int rs) { return t3_cylinder_geometry_new(rt, rb, h, rs, 1, false); }
static t3_geometry *plane(float w, float h) { return t3_plane_geometry_new(w, h, 1, 1); }
static void shad(void *o, bool cast, bool recv) { T3_OBJ(o)->cast_shadow = cast; T3_OBJ(o)->receive_shadow = recv; }
static void rot(void *o, float x, float y, float z) { t3_object_set_rotation(o, x, y, z); }
static t3_object *group(void *parent) { t3_object *g = t3_group_new(); if (parent) add(parent, g); t3_release(g); return g; }
static t3_light *point_light(void *parent, uint32_t c, float in, float dist, float decay, float x, float y, float z) {
  t3_light *l = t3_point_light_new(c, in, dist, decay);
  t3_object_set_position(l, x, y, z);
  add(parent, l); t3_release(l);
  return l;
}

/* a canvas as a texture: Texture/CanvasTexture defaults (flipY, mipmaps,
 * linear, clamp); zs() adds repeat wrapping, its repeat, anisotropy 8 and sRGB */
static t3_texture *cv_tex(cv *c, bool srgb) {
  int w = cv_w(c), h = cv_h(c);
  uint8_t *px = malloc((size_t)w * h * 4);
  cv_read(c, px);
  t3_texture *t = t3_texture_new(w, h, px);
  free(px);
  if (srgb) t->color_space = T3_SRGB_COLOR_SPACE;
  return t;
}
static t3_texture *zs(cv *c, float rx, float ry, float aniso, bool srgb) {
  t3_texture *t = cv_tex(c, srgb);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->repeat.x = rx; t->repeat.y = ry;
  t->anisotropy = aniso;
  return t;
}
static t3_material *std_map(t3_texture *map, uint32_t color, float rough, float metal) {
  t3_material *m = std_mat(color, rough, metal);
  t3_material_set_map(m, map);
  return m;
}

/* Kw: speckle noise */
static void kw(cv *A, float w, float h, int n, float a, bool white) {
  char b[48];
  for (int i = 0; i < n; i++) {
    int g = white ? 255 : 0;
    snprintf(b, sizeof b, "rgba(%d,%d,%d,%g)", g, g, g, ck_rand() * a);
    cv_fill_css(A, b);
    cv_fill_rect(A, ck_rand() * w, ck_rand() * h, ck_rr(1, 3), ck_rr(1, 3));
  }
}
static void fill_circle(cv *c, float x, float y, float r) { cv_begin(c); cv_arc(c, x, y, r, 0, 7, false); cv_fill(c); }

/* O0: text with an "&" set in its own font, centred on x */
static void o0(cv *s, const char *text, float x, float y, int font, float px) {
  const char *amp = strchr(text, '&');
  if (!amp) { cv_font(s, font, px); cv_fill_text(s, text, x, y); return; }
  char a[64], b[64];
  int na = (int)(amp - text); memcpy(a, text, na); a[na] = 0;
  snprintf(b, sizeof b, "%s", amp + 1);
  const char *parts[3] = {a, "&", b};
  cv_font(s, font, px);
  float wsum = 0, w[3];
  for (int i = 0; i < 3; i++) { w[i] = cv_measure(s, parts[i]); wsum += w[i]; }
  float v = x - wsum / 2;
  cv_save(s); cv_text_align(s, CV_LEFT);
  for (int i = 0; i < 3; i++) { cv_fill_text(s, parts[i], v, y); v += w[i]; }
  cv_restore(s);
}

/* ---- the painted textures ---- */

static cv *tl_mega(void) {   /* Tl: the Arduino Mega 2560 */
  cv *e = cv_new(2048, 1074);
#define TT(g) ((g) * 2048.f / 101.6f)
  cv_fill_linear(e, 0, 0, 2048, 1074); cv_fill_stop(e, 0, "#10919a"); cv_fill_stop(e, 1, "#0a6a73");
  cv_fill_rect(e, 0, 0, 2048, 1074);
  cv_stroke_css(e, "rgba(120,220,225,0.18)"); cv_line_width(e, 3);
  for (int g = 0; g < 70; g++) {
    cv_begin(e);
    float i = ck_rr(0, 2048), n = ck_rr(0, 1074);
    cv_move(e, i, n);
    for (int r = 0; r < 5; r++) { if (ck_rand() < 0.5f) i += ck_rr(-160, 160); else n += ck_rr(-120, 120); cv_line(e, i, n); }
    cv_stroke(e);
  }
  cv_fill_css(e, "#f4f1e8"); cv_text_baseline(e, CV_MIDDLE);
  cv_font(e, F_MARKER, 150); cv_text_align(e, CV_LEFT); cv_fill_text(e, "ARDUINO", TT(14), TT(24));
  cv_font(e, F_MARKER, 110); cv_fill_text(e, "MEGA 2560", TT(14), TT(33));
  cv_stroke_css(e, "#f4f1e8"); cv_line_width(e, 22);
  cv_begin(e); cv_ellipse(e, TT(30), TT(40), TT(5), TT(3), 0, 0, 7, false); cv_stroke(e);
  cv_begin(e); cv_ellipse(e, TT(40), TT(40), TT(5), TT(3), 0, 0, 7, false); cv_stroke(e);
  cv_line_width(e, 12);
  cv_begin(e); cv_move(e, TT(27), TT(40)); cv_line(e, TT(33), TT(40)); cv_move(e, TT(37), TT(40)); cv_line(e, TT(43), TT(40)); cv_stroke(e);
  cv_font(e, F_TYPE, 44); cv_fill_css(e, "#f4f1e8"); cv_text_align(e, CV_CENTER);
  cv_fill_text(e, "DIGITAL", TT(66), TT(5.3f)); cv_fill_text(e, "COMMUNICATION", TT(40), TT(49.2f));
  cv_fill_text(e, "POWER", TT(40), TT(44.3f)); cv_fill_text(e, "ANALOG IN", TT(68), TT(44.3f));
  cv_save(e); cv_translate(e, TT(96.2f), TT(28)); cv_rotate(e, PI / 2); cv_fill_text(e, "PWM", 0, 0); cv_restore(e);
  cv_text_align(e, CV_LEFT); cv_font(e, F_TYPE, 36);
  cv_fill_text(e, "ICSP", TT(56), TT(37.2f)); cv_fill_text(e, "RESET", TT(15.3f), TT(24.6f)); cv_fill_text(e, "TX  RX  L  ON", TT(20), TT(46));
  cv_fill_css(e, "#0b0b0d");
  static const float hdr[7][4] = {{27.5f, 1.3f, 20.3f, 2.54f}, {50.5f, 1.3f, 22.8f, 2.54f}, {75, 1.3f, 10.2f, 2.54f}, {27.5f, 49.4f, 20.3f, 2.54f},
                                  {50.5f, 49.4f, 20.3f, 2.54f}, {73, 49.4f, 20.3f, 2.54f}, {93.8f, 4.8f, 5.08f, 45.7f}};
  for (int k = 0; k < 7; k++) {
    float g = hdr[k][0], i = hdr[k][1], n = hdr[k][2], r = hdr[k][3];
    cv_fill_rect(e, TT(g), TT(i), TT(n), TT(r));
    cv_fill_css(e, "#d6a63a");
    int v = (int)roundf(n / 2.54f), o = (int)roundf(r / 2.54f);
    for (int B = 0; B < v; B++) for (int z = 0; z < o; z++) fill_circle(e, TT(g + 1.27f + B * 2.54f), TT(i + 1.27f + z * 2.54f), 9);
    cv_fill_css(e, "#0b0b0d");
  }
  cv_fill_css(e, "#17181b"); cv_fill_rect(e, TT(54), TT(19), TT(13), TT(13));
  cv_fill_css(e, "#d6d6d6"); cv_font(e, F_TYPE, 40); cv_text_align(e, CV_CENTER);
  cv_fill_text(e, "ATMEGA2560", TT(60.5f), TT(24)); cv_fill_text(e, "16AU", TT(60.5f), TT(27));
  cv_fill_css(e, "#17181b"); cv_fill_rect(e, TT(22), TT(9), TT(6), TT(6)); cv_fill_rect(e, TT(34), TT(10), TT(8), TT(5));
  cv_fill_css(e, "#b9bdc4"); cv_begin(e); cv_round_rect(e, TT(37), TT(26), TT(7), TT(3.4f), 20); cv_fill(e);
  cv_fill_css(e, "#c8261b"); fill_circle(e, TT(15.5f), TT(28), TT(1.9f));
  static const struct { float g, i; const char *c; } leds[4] = {{22, 43, "#5fb892"}, {25, 43, "#f2ab1e"}, {28, 43, "#f2ab1e"}, {31, 43, "#f2ab1e"}};
  for (int k = 0; k < 4; k++) { cv_fill_css(e, leds[k].c); cv_fill_rect(e, TT(leds[k].g), TT(leds[k].i), TT(1.6f), TT(1.2f)); }
  cv_fill_css(e, "#071e21");
  static const float holes[5][2] = {{15.2f, 50.3f}, {90.2f, 50.8f}, {96.5f, 2.5f}, {66.1f, 35.5f}, {66.1f, 7.7f}};
  for (int k = 0; k < 5; k++) {
    cv_begin(e); cv_arc(e, TT(holes[k][0]), TT(holes[k][1]), TT(1.6f), 0, 7, false); cv_fill(e);
    cv_stroke_css(e, "#d6a63a"); cv_line_width(e, 8); cv_stroke(e);
  }
  char b[40];
  for (int g = 0; g < 2000; g++) {
    snprintf(b, sizeof b, "rgba(0,0,0,%g)", ck_rand() * 0.08f); cv_fill_css(e, b);
    cv_fill_rect(e, ck_rr(0, 2048), ck_rr(0, 1074), ck_rr(1, 5), ck_rr(1, 3));
  }
#undef TT
  return e;
}

static cv *ul_floor(void) {
  cv *A = cv_new(1024, 1024); char b[64];
  cv_fill_css(A, "#6c6d70"); cv_fill_rect(A, 0, 0, 1024, 1024);
  for (int P = 0; P < 2600; P++) {
    int e = (int)floorf(ck_rr(80, 140));
    snprintf(b, sizeof b, "rgba(%d,%d,%d,%g)", e, e, e + 4, ck_rr(0.04f, 0.14f)); cv_fill_css(A, b);
    float t = ck_rr(4, 40);
    float x = ck_rand() * 1024, y = ck_rand() * 1024;
    fill_circle(A, x, y, t);
  }
  for (int P = 0; P < 9; P++) {
    float e = ck_rand() * 1024, t = ck_rand() * 1024, D = ck_rr(30, 90);
    cv_fill_radial(A, e, t, 0, e, t, D); cv_fill_stop(A, 0, "rgba(20,20,22,0.35)"); cv_fill_stop(A, 1, "rgba(20,20,22,0)");
    cv_fill_rect(A, e - D, t - D, D * 2, D * 2);
  }
  cv_stroke_css(A, "rgba(20,20,22,0.55)"); cv_line_width(A, 3);
  cv_begin(A); cv_move(A, 0, 512); cv_line(A, 1024, 512); cv_move(A, 512, 0); cv_line(A, 512, 1024); cv_stroke(A);
  cv_stroke_css(A, "rgba(20,20,22,0.25)"); cv_line_width(A, 1);
  for (int P = 0; P < 14; P++) {
    cv_begin(A);
    float e = ck_rand() * 1024, t = ck_rand() * 1024;
    cv_move(A, e, t);
    for (int D = 0; D < 8; D++) { e += ck_rr(-30, 30); t += ck_rr(-30, 30); cv_line(A, e, t); }
    cv_stroke(A);
  }
  kw(A, 1024, 1024, 14000, 0.12f, false); kw(A, 1024, 1024, 6000, 0.06f, true);
  return A;
}

static cv *ll_wall(void) {
  cv *A = cv_new(1024, 512); char b[64];
  cv_fill_css(A, "#4a4f57"); cv_fill_rect(A, 0, 0, 1024, 512);
  for (int e = 0; e < 64; e++) {
    float t = e * 16.f;
    cv_fill_linear(A, t, 0, t + 16, 0);
    cv_fill_stop(A, 0, "#6b7078"); cv_fill_stop(A, 0.45f, "#555a62"); cv_fill_stop(A, 0.55f, "#3b3f46"); cv_fill_stop(A, 1, "#2e3238");
    cv_fill_rect(A, t, 0, 16, 512);
  }
  cv_stroke_css(A, "rgba(10,10,12,0.8)"); cv_line_width(A, 5);
  static const float ys[3] = {0, 256, 512};
  for (int i = 0; i < 3; i++) { cv_begin(A); cv_move(A, 0, ys[i]); cv_line(A, 1024, ys[i]); cv_stroke(A); }
  cv_fill_css(A, "rgba(20,20,22,0.85)");
  static const float rows[4] = {14, 242, 270, 498};
  for (int i = 0; i < 4; i++) for (float t = 8; t < 1024; t += 32) fill_circle(A, t, rows[i], 3.2f);
  for (int e = 0; e < 70; e++) {
    float t = ck_rand() * 1024, D = ck_rand() * 300, w = ck_rr(60, 260);
    cv_fill_linear(A, 0, D, 0, D + w);
    snprintf(b, sizeof b, "rgba(150,70,30,%g)", ck_rr(0.12f, 0.4f));
    cv_fill_stop(A, 0, b); cv_fill_stop(A, 1, "rgba(150,70,30,0)");
    cv_fill_rect(A, t, D, ck_rr(2, 9), w);
  }
  cv_fill_linear(A, 0, 0, 0, 512);
  cv_fill_stop(A, 0, "rgba(0,0,0,0.35)"); cv_fill_stop(A, 0.5f, "rgba(0,0,0,0)"); cv_fill_stop(A, 1, "rgba(0,0,0,0.5)");
  cv_fill_rect(A, 0, 0, 1024, 512);
  kw(A, 1024, 512, 9000, 0.16f, false);
  return A;
}

/* dl: the pegboard with its tools; each shape is drawn as a shadow, then filled and outlined */
typedef void (*shape_fn)(cv *, float);
static void sh_wrench(cv *e, float r) {
  float v = 16;
  cv_begin(e);
  cv_move(e, -v / 2, r / 2); cv_line(e, v / 2, r / 2); cv_line(e, v * 0.9f, -r * 0.2f); cv_line(e, 46, -r * 0.26f);
  cv_line(e, 46, -r * 0.42f); cv_line(e, 24, -r * 0.5f); cv_line(e, 10, -r * 0.5f); cv_line(e, 10, -r * 0.38f);
  cv_line(e, -10, -r * 0.38f); cv_line(e, -10, -r * 0.5f); cv_line(e, -24, -r * 0.5f); cv_line(e, -46, -r * 0.42f);
  cv_line(e, -46, -r * 0.26f); cv_line(e, -v * 0.9f, -r * 0.2f); cv_close(e);
}
static void sh_hammer(cv *e, float r) { (void)r; cv_begin(e); cv_rect(e, -9, -60, 18, 230); cv_rect(e, -60, -110, 120, 56); }
static void sh_driver(cv *e, float r) { (void)r; cv_begin(e); cv_rect(e, -5, -20, 10, 150); cv_rect(e, -14, -130, 28, 110); }
static void sh_pliers(cv *e, float r) {
  (void)r; cv_begin(e);
  cv_move(e, -8, -120); cv_line(e, 8, -120); cv_line(e, 24, 0); cv_line(e, 20, 150); cv_line(e, 6, 150); cv_line(e, 0, 20);
  cv_line(e, -6, 150); cv_line(e, -20, 150); cv_line(e, -24, 0); cv_close(e);
}
static void sh_saw(cv *e, float r) {
  (void)r; cv_begin(e); cv_move(e, -170, -10); cv_line(e, 110, -60); cv_line(e, 110, 40); cv_line(e, -170, 40); cv_close(e);
}
static void peg(cv *e, shape_fn f, float r, float v, float o, float B, const char *z) {
  cv_save(e); cv_translate(e, v + 8, o + 10); cv_rotate(e, B); cv_fill_css(e, "rgba(30,18,8,0.45)"); f(e, r); cv_fill(e); cv_restore(e);
  cv_save(e); cv_translate(e, v, o); cv_rotate(e, B); cv_fill_css(e, z); f(e, r); cv_fill(e);
  cv_line_width(e, 5); cv_stroke_css(e, "#ece3d3"); cv_stroke(e); cv_restore(e);
}
static cv *dl_pegboard(void) {
  cv *e = cv_new(1536, 768); char b[48];
  cv_fill_css(e, "#a77c4c"); cv_fill_rect(e, 0, 0, 1536, 768);
  for (int r = 0; r < 9000; r++) {
    snprintf(b, sizeof b, "rgba(60,35,15,%g)", ck_rand() * 0.12f); cv_fill_css(e, b);
    cv_fill_rect(e, ck_rand() * 1536, ck_rand() * 768, ck_rr(1, 4), ck_rr(1, 2));
  }
  cv_fill_css(e, "rgba(45,25,10,0.8)");
  for (int r = 18; r < 768; r += 36) for (int v = 18; v < 1536; v += 36) fill_circle(e, (float)v, (float)r, 4.2f);
  peg(e, sh_wrench, 330, 120, 380, 0.12f, "#f2ab1e"); peg(e, sh_wrench, 300, 240, 390, -0.05f, "#d7dde4");
  peg(e, sh_wrench, 260, 345, 400, 0.05f, "#d7dde4"); peg(e, sh_hammer, 0, 520, 320, -0.35f, "#1f9d55");
  peg(e, sh_driver, 0, 650, 330, 0.1f, "#f2ab1e"); peg(e, sh_driver, 0, 730, 340, 0.1f, "#5e93c4");
  peg(e, sh_pliers, 0, 830, 330, 0.15f, "#c8261b"); peg(e, sh_saw, 0, 1130, 250, 0, "#e8e0d0");
  peg(e, sh_wrench, 280, 1300, 380, 0.2f, "#f2ab1e"); peg(e, sh_hammer, 0, 1420, 330, 0.25f, "#5e93c4");
  cv_begin(e); cv_arc(e, 1000, 520, 70, 0, 7, false); cv_line_width(e, 14); cv_stroke_css(e, "#f2ab1e"); cv_stroke(e);
  kw(e, 1536, 768, 6000, 0.1f, false);
  return e;
}

static cv *lc_hazard(void) {
  cv *A = cv_new(256, 256);
  cv_fill_css(A, "#16130f"); cv_fill_rect(A, 0, 0, 256, 256); cv_fill_css(A, "#f2ab1e");
  for (int P = -4; P < 8; P++) {
    cv_begin(A); cv_move(A, P * 64.f, 256); cv_line(A, P * 64.f + 64, 256); cv_line(A, P * 64.f + 192, 0); cv_line(A, P * 64.f + 128, 0); cv_close(A); cv_fill(A);
  }
  kw(A, 256, 256, 1500, 0.18f, false);
  return A;
}

static cv *bl_decal(void) {
  cv *P = cv_new(2048, 2048);
  cv_translate(P, 1024, 1024);
  float e = 1024 - 8;
#define TB(w) ((w) / 5.6f * e)
  cv_fill_css(P, "rgba(18,18,22,0.28)"); fill_circle(P, 0, 0, TB(4.7f));
  cv *lc = lc_hazard();
  uint8_t *pat = malloc(256 * 256 * 4); cv_read(lc, pat); cv_free(lc);
  cv_save(P);
  cv_begin(P); cv_arc(P, 0, 0, TB(5.1f), 0, 7, false); cv_arc(P, 0, 0, TB(4.75f), 0, 7, true); cv_clip(P);
  cv_set_transform(P, 1, 0, 0, 1, 0, 0);
  cv_fill_pattern(P, pat, 256, 256); cv_fill_rect(P, 0, 0, 2048, 2048);
  cv_restore(P);
  free(pat);
  cv_stroke_css(P, "#ece3d3"); cv_line_width(P, 7); cv_begin(P); cv_arc(P, 0, 0, TB(4.7f), 0, 7, false); cv_stroke(P);
  cv_stroke_css(P, "#f2ab1e"); cv_line_width(P, 14); cv_begin(P); cv_arc(P, 0, 0, TB(1.55f), 0, 7, false); cv_stroke(P);
  cv_line_width(P, 6);
  for (int w = 0; w < 24; w++) {
    float g = w / 24.f * PI * 2;
    cv_begin(P); cv_move(P, cosf(g) * TB(4.1f), sinf(g) * TB(4.1f)); cv_line(P, cosf(g) * TB(4.5f), sinf(g) * TB(4.5f)); cv_stroke(P);
  }
  cv_fill_css(P, "#ece3d3");
  for (int k = -1; k <= 1; k += 2) { float w = 2.4f * k; cv_fill_rect(P, TB(w) - TB(0.22f), -TB(0.04f), TB(0.44f), TB(0.08f)); }
  cv_save(P); cv_rotate(P, -0.07f);
  cv_fill_css(P, "rgba(245,240,230,0.9)"); cv_text_align(P, CV_CENTER); cv_text_baseline(P, CV_MIDDLE);
  cv_font(P, F_TYPE, TB(0.3f)); cv_fill_text(P, "H A C K E R S P A C E   B R A W L", 0, -TB(0.55f));
  cv_fill_css(P, "#f2ab1e"); cv_font(P, F_MARKER, TB(0.6f)); cv_fill_text(P, "COFFEE", 0, -TB(0.06f));
  cv_fill_css(P, "#ece3d3"); o0(P, "& KILL", 0, TB(0.5f), F_MARKER, TB(0.6f));
  cv_restore(P);
  char b[40];
  for (int w = 0; w < 500; w++) {
    snprintf(b, sizeof b, "rgba(0,0,0,%g)", ck_rand() * 0.18f); cv_fill_css(P, b);
    cv_fill_rect(P, ck_rr(-e, e), ck_rr(-e, e), ck_rr(2, 24), ck_rr(1, 3));
  }
#undef TB
  return P;
}

static cv *ll_sign(void) {
  cv *A = cv_new(2048, 640);
  cv_fill_css(A, "#0a0a0a"); cv_fill_rect(A, 0, 0, 2048, 640);
  cv_stroke_css(A, "#f2ab1e"); cv_line_width(A, 14); cv_stroke_rect(A, 22, 22, 2004, 596);
  cv_fill_css(A, "#ece3d3"); cv_font(A, F_TYPE, 96); cv_text_align(A, CV_CENTER);
  cv_fill_text(A, "O P E N   L A T E", 1024, 150);
  cv_shadow(A, "#c2542c", 0, 14, 14); cv_fill_css(A, "#f2ab1e");
  o0(A, "COFFEE & KILL", 1024, 450, F_MARKER, 245);
  cv_no_shadow(A);
  return A;
}

static cv *xl_shutter(void) {
  cv *A = cv_new(512, 512);
  cv_fill_css(A, "#8a9096"); cv_fill_rect(A, 0, 0, 512, 512);
  for (int P = 0; P < 32; P++) {
    float e = P * 16.f;
    cv_fill_linear(A, 0, e, 0, e + 16); cv_fill_stop(A, 0, "#a9afb5"); cv_fill_stop(A, 0.5f, "#80868c"); cv_fill_stop(A, 1, "#5f656b");
    cv_fill_rect(A, 0, e, 512, 15);
  }
  kw(A, 512, 512, 5000, 0.2f, false);
  return A;
}

static cv *nl_cone(void) {
  cv *A = cv_new(64, 256);
  cv_fill_linear(A, 0, 0, 0, 256); cv_fill_stop(A, 0, "rgba(255,240,210,0.9)"); cv_fill_stop(A, 1, "rgba(255,240,210,0)");
  cv_fill_rect(A, 0, 0, 64, 256);
  cv_fill_linear(A, 0, 0, 64, 0); cv_fill_stop(A, 0, "rgba(0,0,0,1)"); cv_fill_stop(A, 0.5f, "rgba(0,0,0,0)"); cv_fill_stop(A, 1, "rgba(0,0,0,1)");
  cv_comp(A, CV_DEST_OUT); cv_fill_rect(A, 0, 0, 64, 256); cv_comp(A, CV_SOURCE_OVER);
  return A;
}

static cv *pl_dot(void) {
  cv *A = cv_new(64, 64);
  cv_fill_radial(A, 32, 32, 0, 32, 32, 32); cv_fill_stop(A, 0, "rgba(255,255,255,1)"); cv_fill_stop(A, 1, "rgba(255,255,255,0)");
  cv_fill_rect(A, 0, 0, 64, 64);
  return A;
}

static cv *ul_neon(void) {
  cv *A = cv_new(1024, 320);
  cv_clear_rect(A, 0, 0, 1024, 320);
  cv_text_align(A, CV_CENTER); cv_text_baseline(A, CV_MIDDLE);
  cv_shadow(A, "#ffb04a", 50, 0, 0); cv_fill_css(A, "#ffc46a"); cv_font(A, F_MARKER, 210); cv_fill_text(A, "COFFEE", 512, 170);
  cv_shadow(A, "#ffb04a", 8, 0, 0); cv_fill_css(A, "#fff3d6"); cv_fill_text(A, "COFFEE", 512, 170);
  cv_no_shadow(A);
  cv_stroke_css(A, "#f2ab1e"); cv_line_width(A, 10); cv_stroke_rect(A, 30, 30, 964, 260);
  return A;
}

/* ---- updaters ---- */

typedef struct upd {
  void (*fn)(struct upd *, float dt, float t);
  void *a, *b, *c;
  float p[6];
} upd;
static upd ups[96]; static int nups;
static upd *push_upd(void (*fn)(upd *, float, float)) { upd *u = &ups[nups++]; memset(u, 0, sizeof *u); u->fn = fn; return u; }

/* ---- G0: the Arduino Mega on the wall ---- */
static struct { t3_object *pivot, *n; t3_mesh *leds[4]; } mega;
static void mega_update(upd *u, float dt, float t) {
  (void)u; (void)dt;
  bool tA = sinf(t * 3) > 0.2f, iA = sinf(t * 4.7f + 1) > 0;
  T3_OBJ(mega.leds[1])->visible = tA; T3_OBJ(mega.leds[2])->visible = iA; T3_OBJ(mega.leds[3])->visible = !iA;
}
static void g0_mega(float P, float e, float t, float D) {
  float w = t / 1.906f, g = 0.14f;
  t3_object *i = group(T);
  t3_object_set_position(i, P, 0, e); rot(i, -D, 0, 0);
  t3_object *n = group(i);
  n->position.y = w / 2;
  mega.pivot = i; mega.n = n;
  cv *c = tl_mega();
  t3_texture *r = cv_tex(c, true); r->anisotropy = 8; cv_free(c);
  t3_material *v = std_mat(744033, 0.6f, 0);
  t3_material *face = std_map(r, 0xffffff, 0.45f, 0.1f); t3_release(r);
  t3_material *ms[6] = {v, v, v, v, face, v};
  t3_geometry *bg = box(t, w, g);
  t3_mesh *o = t3_mesh_new_multi(bg, ms, 6); t3_release(bg); t3_release(v); t3_release(face);
  shad(o, true, true); add(n, o); t3_release(o);
#define BX(d) (((d) / 101.6f - 0.5f) * t)
#define ZZ(d) ((0.5f - (d) / 53.3f) * w)
  float a = t / 101.6f;
  t3_material *Q = std_mat(921104, 0.5f, 0), *m = std_mat(12830668, 0.28f, 0.95f), *j = std_mat(14067258, 0.3f, 0.9f);
  float f = g / 2;
#define CM(geo, mat, x, y, z) ({ t3_mesh *_h = mesh_k(n, geo, mat, x, y, z); shad(_h, true, true); _h; })
  static const float hdr[7][4] = {{27.5f, 1.3f, 20.3f, 2.54f}, {50.5f, 1.3f, 22.8f, 2.54f}, {75, 1.3f, 10.2f, 2.54f}, {27.5f, 49.4f, 20.3f, 2.54f},
                                  {50.5f, 49.4f, 20.3f, 2.54f}, {73, 49.4f, 20.3f, 2.54f}, {93.8f, 4.8f, 5.08f, 45.7f}};
  for (int k = 0; k < 7; k++) {
    float dd = hdr[k][0], bb = hdr[k][1], N = hdr[k][2], y = hdr[k][3];
    float x = BX(dd + N / 2), H = ZZ(bb + y / 2);
    CM(box(N * a, y * a, 0.5f * a * 2), Q, x, H, f + 0.5f * a);
    int Y = (int)roundf(N / 2.54f), G = (int)roundf(y / 2.54f);
    for (int V = 0; V < Y; V++) for (int Sx = 0; Sx < G; Sx++)
      CM(box(0.64f * a, 0.64f * a, 5 * a), j, BX(dd + 1.27f + V * 2.54f), ZZ(bb + 1.27f + Sx * 2.54f), f + 3 * a);
  }
  CM(box(16 * a, 12 * a, 11 * a), m, BX(6), ZZ(14), f + 5.5f * a);
  { t3_material *mm = std_mat(1381655, 0.5f, 0); CM(box(14 * a, 9 * a, 11 * a), mm, BX(7), ZZ(41), f + 5.5f * a); t3_release(mm); }
  { t3_material *mm = std_mat(1447706, 0.4f, 0); CM(box(13 * a, 13 * a, 1.6f * a), mm, BX(60.5f), ZZ(25.5f), f + 0.8f * a); t3_release(mm); }
  CM(box(7 * a, 3.4f * a, 3 * a), m, BX(40.5f), ZZ(27.7f), f + 1.5f * a);
  { t3_material *mm = std_mat(13116955, 0.5f, 0); t3_mesh *Tm = CM(cyl(1.9f * a, 1.9f * a, 3.5f * a, 16), mm, BX(15.5f), ZZ(28), f + 1.75f * a); rot(Tm, PI / 2, 0, 0); t3_release(mm); }
  static const float caps[6][2] = {{24, 22}, {27, 22}, {30, 22}, {24, 30}, {28, 33}, {46, 36}};
  for (int k = 0; k < 6; k++) {
    t3_material *mm = std_mat(2106410, 0.4f, 0.4f);
    t3_mesh *Nm = CM(cyl(1.6f * a, 1.6f * a, 5 * a, 14), mm, BX(caps[k][0]), ZZ(caps[k][1]), f + 2.5f * a); rot(Nm, PI / 2, 0, 0);
    t3_release(mm);
  }
  static const struct { float d; uint32_t b; } ld[4] = {{22.8f, 6273170}, {25.8f, 15903518}, {28.8f, 15903518}, {31.8f, 15903518}};
  for (int k = 0; k < 4; k++) mega.leds[k] = mesh(n, box(1.6f * a, 1.2f * a, 0.8f * a), basic_mat(ld[k].b, false), BX(ld[k].d), ZZ(43.6f), f + 0.5f * a);
  point_light(n, 15903518, 18, 6, 2, BX(28), ZZ(43), f + 1.2f);
  /* the six jumper cables: from the header in world space to the floor in front */
  t3_object_update_matrix_world(T3_OBJ(S), true);
  static const uint32_t cc[6] = {12735532, 6198212, 6273170, 15903518, 15524819, 13782842};
  for (int b = 0; b < 6; b++) {
    t3_vec3 N = t3_v3(BX(30 + b * 3.8f), ZZ(2.5f), f + 0.4f);
    t3_vec3 H = t3_vec3_apply_mat4(N, &n->matrix_world);
    t3_vec3 Y = t3_v3(P - 1.8f + b * 0.7f + ck_rr(-0.2f, 0.2f), 0.07f, e + 1.6f + ck_rr(0, 1.2f));
    t3_vec3 x[4];
    x[0] = H;
    x[1] = t3_v3(H.x + ck_rr(-0.4f, 0.4f), H.y + 0.7f, H.z + 0.5f);
    x[2] = t3_v3((H.x + Y.x) / 2 + ck_rr(-0.5f, 0.5f), fmaxf(1.2f, (H.y + Y.y) / 2), H.z + 1.2f);
    x[3] = Y;
    t3_mesh *V = mesh(T, geom_tube(x, 4, false, 1, 0.5f, 40, 0.05f, 6, false), std_mat(cc[b], 0.55f, 0), 0, 0, 0);
    shad(V, true, false);
    t3_mesh *Sb = mesh_k(T, box(0.16f, 0.16f, 0.4f), Q, H.x, H.y, H.z);
    rot(Sb, -D, 0, 0);
  }
  t3_release(Q); t3_release(m); t3_release(j);
#undef CM
#undef BX
#undef ZZ
  push_upd(mega_update);
}

/* ---- k0: the robot eye ---- */
static struct { t3_object *group; t3_light *eye_light; } eye;
static void eye_update(upd *u, float X, float PA) {
  (void)u;
  t3_object *g = eye.group;
  t3_object_update_world_matrix(g, true, false);
  t3_mat4 inv; t3_mat4_invert(&inv, &g->matrix_world);
  t3_vec3 G = t3_vec3_apply_mat4(focus, &inv);
  float tA = atan2f(G.x, G.z), iA = atan2f(-(G.y - 0.1f), hypotf(G.x, G.z));
  float k = fminf(1, X * 3);
  float ry = ck_lerp(g->rotation.y, fmaxf(-0.7f, fminf(0.7f, tA + g->rotation.y)), k);
  float rx = ck_lerp(g->rotation.x, fmaxf(-0.4f, fminf(0.5f, iA + g->rotation.x)), k);
  rot(g, rx, ry, g->rotation.z);
  float cA = 0.7f + 0.3f * sinf(PA * 3);
  eye.eye_light->intensity = 30 * cA;
}
static void k0_eye(float P, float e, float t, float D) {
  t3_object *w = group(T);
  t3_object_set_position(w, P, e, t);
  t3_material *g = std_mat(9212570, 0.35f, 0.9f), *i = std_mat(1776930, 0.5f, 0.6f), *n = basic_mat(15903518, false);
  float r = D;
#define V(geo, mat, x, y, z) ({ t3_mesh *_j = mesh_k(w, geo, mat, x, y, z); _j->base.cast_shadow = true; _j; })
  V(box(r, r * 0.8f, r * 0.9f), g, 0, 0, 0);
  V(box(r * 1.06f, r * 0.12f, r * 0.96f), i, 0, r * 0.3f, 0);
  V(box(r * 0.5f, r * 0.4f, r * 0.5f), i, 0, -r * 0.58f, 0);
  for (int z = -1; z <= 1; z += 2) {
    t3_mesh *a = V(cyl(r * 0.2f, r * 0.2f, r * 0.1f, 24), i, z * r * 0.24f, r * 0.08f, r * 0.45f); rot(a, PI / 2, 0, 0);
    V(t3_sphere_geometry_new(r * 0.13f, 20, 16), n, z * r * 0.24f, r * 0.08f, r * 0.5f);
  }
  for (int z = 0; z < 6; z++) V(box(r * 0.5f, r * 0.03f, r * 0.04f), i, 0, -r * 0.2f - z * r * 0.055f, r * 0.46f);
  t3_material *ant = basic_mat(13782842, false);
  for (int z = -1; z <= 1; z += 2) {
    V(cyl(0.03f, 0.03f, r * 0.7f, 8), g, z * r * 0.32f, r * 0.7f, 0);
    V(t3_sphere_geometry_new(0.1f, 12, 10), ant, z * r * 0.32f, r * 1.06f, 0);
  }
  for (int z = -1; z <= 1; z += 2) mesh_k(w, cyl(0.05f, 0.05f, 8.2f - e, 6), i, z * r * 0.35f, r * 0.4f + (8.2f - e) / 2, 0);
#undef V
  t3_release(g); t3_release(i); t3_release(n); t3_release(ant);
  eye.group = w;
  eye.eye_light = point_light(w, 15903518, 30, 9, 2, 0, 0.1f, r * 0.8f);
  push_upd(eye_update);
}

/* ---- ml / jl: the little screens ---- */
typedef struct { cv *c; t3_texture *tex; int kind; } scr;
static scr screens[16]; static int nscreens;
static void jl_draw(scr *s, int A, float P) {
  cv *e = s->c; char b[64];
  cv_fill_css(e, "#0a0f0c"); cv_fill_rect(e, 0, 0, 256, 192);
  cv_fill_css(e, "#f2ab1e"); cv_stroke_css(e, "#f2ab1e"); cv_line_width(e, 4);
  if (A == 0) {
    bool t = sinf(P * 1.7f) > 0.93f;
    static const float xs[2] = {78, 178};
    for (int k = 0; k < 2; k++) {
      float D = xs[k];
      if (t) cv_fill_rect(e, D - 24, 98, 48, 6);
      else fill_circle(e, D, 92 + sinf(P * 2 + D) * 6, 22);
    }
    cv_fill_rect(e, 88, 148, 80, 8);
  } else if (A == 1) {
    for (int t = 0; t < 16; t++) { float D = 20 + fabsf(sinf(P * 3 + t * 0.7f)) * 130; cv_fill_rect(e, 12 + t * 15.f, 176 - D, 10, D); }
  } else if (A == 2) {
    cv_font(e, F_TYPE, 14); cv_text_align(e, CV_LEFT);
    for (int t = 0; t < 11; t++) {
      char line[40]; int o = 0;
      for (int w = 0; w < 8; w++) {
        int v = (int)((sinf(P * 0.9f + t * 5.3f + w * 1.9f) * 0.5f + 0.5f) * 255) | 0;
        o += snprintf(line + o, sizeof line - o, "%02x ", v & 255);
      }
      cv_fill_text(e, line, 10, 18 + t * 16.f);
    }
  } else {
    cv_font(e, F_MARKER, 34); cv_text_align(e, CV_CENTER);
    cv_fill_text(e, ((int)floorf(P)) % 2 ? "ERROR 418" : "BREW FAILED", 128, 84);
    cv_font(e, F_TYPE, 15); cv_fill_text(e, "TEAPOT NOT FOUND", 128, 118);
    cv_fill_rect(e, 30, 140, 196 * (0.5f + 0.5f * sinf(P)), 10);
  }
  (void)b;
  cv_fill_css(e, "rgba(0,0,0,0.18)");
  for (int t = 0; t < 192; t += 4) cv_fill_rect(e, 0, (float)t, 256, 1);
  cv_read(e, s->tex->pixels);   /* needsUpdate */
  s->tex->version++;
}
static void qr_update(upd *u, float z, float a) {
  u->p[0] += z;
  if (u->p[0] < 0.2f) return;
  u->p[0] = 0;
  int from = (int)u->p[1], cnt = (int)u->p[2];
  for (int k = 0; k < cnt; k++) { scr *c = &screens[from + k]; jl_draw(c, c->kind, a + c->kind * 3); }
}
static void qr_screens(float P, float e, float t, float D, int w, int g, float i, float n) {
  t3_object *v = group(T);
  t3_object_set_position(v, P, e, t); rot(v, 0, D, 0);
  t3_material *o = std_mat(1382171, 0.5f, 0.6f);
  int from = nscreens;
  for (int z = 0; z < g; z++) for (int a = 0; a < w; a++) {
    scr *c = &screens[nscreens++];
    c->c = cv_new(256, 192);
    uint8_t *px = calloc(256 * 192, 4);
    c->tex = t3_texture_new(256, 192, px); free(px);
    c->tex->color_space = T3_SRGB_COLOR_SPACE;
    c->kind = (z * w + a) % 4;
    t3_mesh *m = mesh_k(v, box(i + 0.12f, n + 0.12f, 0.3f), o, (a - (w - 1) / 2.f) * (i + 0.18f), (z - (g - 1) / 2.f) * (n + 0.18f), 0);
    t3_material *sm = basic_mat(0xffffff, false); t3_material_set_map(sm, c->tex);
    mesh(m, plane(i, n), sm, 0, 0, 0.16f);
  }
  t3_release(o);
  upd *u = push_upd(qr_update);
  u->p[1] = (float)from; u->p[2] = (float)(nscreens - from);
}

/* ---- F0: the hanging cables ---- */
static void f0_cables(t3_vec3 (*seg)[2], const float *sag, int n) {
  t3_material *e = std_mat(1447963, 0.8f, 0);
  for (int k = 0; k < n; k++) {
    t3_vec3 g = seg[k][0], i = seg[k][1], m = t3_vec3_lerp(g, i, 0.5f);
    m.y -= sag[k];
    t3_vec3 pts[3] = {g, m, i};
    t3_mesh *r = mesh_k(T, geom_tube(pts, 3, false, 1, 0.5f, 30, 0.07f, 6, false), e, 0, 0, 0);
    r->base.cast_shadow = true;
  }
  t3_release(e);
}

/* ---- Kr: the beacons (the beam plane is an fx draw) ---- */
typedef struct { t3_object *spin, *beam; t3_light *light; float col[3]; } beacon;
static beacon beacons[3]; static int nbeacons;
static void kr_update(upd *u, float o, float B) {
  (void)o;
  beacon *b = (beacon *)u->a;
  rot(b->spin, 0, B * 4, 0);
  b->light->intensity = 12 + 14 * fmaxf(0, sinf(B * 4));
}
static void kr_beacon(float e, float t, float D, uint32_t w) {
  beacon *b = &beacons[nbeacons++];
  t3_object *g = group(T);
  t3_object_set_position(g, e, t, D);
  mesh(g, cyl(0.22f, 0.28f, 0.18f, 16), std_mat(1711136, 0.5f, 0.6f), 0, 0, 0);
  t3_material *dm = basic_mat(w, false); dm->transparent = true; dm->opacity = 0.9f;
  mesh(g, t3_sphere_geometry_new_ex(0.2f, 16, 12, 0, PI * 2, 0, PI / 2), dm, 0, 0.09f, 0);
  t3_object *n = group(g); n->position.y = 0.18f;
  t3_object *r = group(n); r->position.x = 3;   /* the RP(6, .5) beam, drawn in arena_fx_draw */
  b->spin = n; b->beam = r;
  fx_hex_linear(w, b->col);
  b->light = point_light(g, w, 25, 10, 2, 0, 0.4f, 0);
  upd *u = push_upd(kr_update); u->a = b;
}

/* ---- Y0: the anvil's sparks ---- */
static struct { float e[420], t[420], D[140], i; t3_vec3 at; } sparks;
static void y0_update(upd *u, float n, float tt) {
  (void)u; (void)tt;
  float *e = sparks.e, *t = sparks.t, *D = sparks.D;
  sparks.i -= n;
  if (sparks.i <= 0) {
    sparks.i = ck_rr(0.35f, 1.4f);
    int r = (int)ck_rr(25, 60);
    for (int v = 0, o = 0; v < r && o < 140; o++) {
      if (D[o] > 0) continue;
      v++; D[o] = ck_rr(0.4f, 1.1f);
      e[o * 3] = sparks.at.x; e[o * 3 + 1] = sparks.at.y; e[o * 3 + 2] = sparks.at.z;
      float B = ck_rr(0, 6.28f), z = ck_rr(1.5f, 4.5f);
      t[o * 3] = cosf(B) * z; t[o * 3 + 1] = ck_rr(1, 4); t[o * 3 + 2] = sinf(B) * z * 0.6f + 1;
    }
  }
  for (int r = 0; r < 140; r++) {
    if (D[r] <= 0) { e[r * 3 + 1] = -50; continue; }
    D[r] -= n; t[r * 3 + 1] -= 9 * n;
    e[r * 3] += t[r * 3] * n; e[r * 3 + 1] += t[r * 3 + 1] * n; e[r * 3 + 2] += t[r * 3 + 2] * n;
    if (e[r * 3 + 1] < 0.02f) { e[r * 3 + 1] = 0.02f; t[r * 3 + 1] *= -0.3f; }
  }
}

/* ---- T(): a Kenney prop ---- */
typedef struct { float x, y, z, ry, h, s; bool shadow; bool has_rot; float rot[3]; } prop_opt;
static void prop_mat(t3_object *o, void *ctx) {
  bool cast = *(bool *)ctx;
  if (o->type != T3_MESH && o->type != T3_SKINNED_MESH) return;
  t3_mesh *m = (t3_mesh *)o;
  o->cast_shadow = cast; o->receive_shadow = true;
  for (int i = 0; i < m->material_count; i++) {
    t3_material *c = t3_material_clone(m->materials[i]);
    c->color.r = c->color.g = c->color.b = 0.72f;
    c->roughness = 0.85f; c->metalness = 0.05f;
    if (c->map) { c->map->mag_filter = T3_LINEAR; c->map->anisotropy = 4; }
    t3_release(m->materials[i]); m->materials[i] = c;
  }
}
static void box_expand(t3_object *o, void *ctx) {
  t3_box3 *b = ctx;
  if (o->type != T3_MESH && o->type != T3_SKINNED_MESH && o->type != T3_INSTANCED_MESH) return;
  t3_geometry *g = ((t3_mesh *)o)->geometry;
  if (!g->has_bounding_box) t3_geometry_compute_bounding_box(g);
  t3_box3 gb = g->bounding_box;
  for (int k = 0; k < 8; k++) {
    t3_vec3 p = t3_v3(k & 1 ? gb.max.x : gb.min.x, k & 2 ? gb.max.y : gb.min.y, k & 4 ? gb.max.z : gb.min.z);
    p = t3_vec3_apply_mat4(p, &o->matrix_world);
    if (p.x < b->min.x) b->min.x = p.x;
    if (p.y < b->min.y) b->min.y = p.y;
    if (p.z < b->min.z) b->min.z = p.z;
    if (p.x > b->max.x) b->max.x = p.x;
    if (p.y > b->max.y) b->max.y = p.y;
    if (p.z > b->max.z) b->max.z = p.z;
  }
}
static t3_box3 box_of(t3_object *F) {
  t3_box3 b = {{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}};
  t3_object_traverse(F, box_expand, &b);
  return b;
}
/* the props a camera must not pass into: their world boxes, a little grown (the arms sway) */
static t3_box3 posts[40]; static int nposts;
int arena_obstacles(const t3_box3 **boxes) { *boxes = posts; return nposts; }
static t3_object *prop(const char *name, prop_opt o) {
  char path[64]; snprintf(path, sizeof path, "%s.glb", name);
  size_t len; uint8_t *bytes = ck_asset(path, &len);
  if (!bytes) return NULL;
  t3_gltf *gl = t3_gltf_parse(bytes, len, NULL, NULL);
  free(bytes);
  if (!gl) { ck_log("arena: bad model %s", name); return NULL; }
  t3_object *F = gl->scene;   /* the t3_gltf owns the names: kept alive with the scene */
  t3_object_traverse(F, prop_mat, &o.shadow);
  t3_object_update_matrix_world(F, true);
  t3_box3 q = box_of(F);
  float oA = o.s > 0 ? o.s : 1;
  if (o.h > 0) oA = o.h / (q.max.y - q.min.y);
  t3_object_set_scale(F, oA, oA, oA);
  t3_object_update_matrix_world(F, true);
  t3_box3 BA = box_of(F);
  float eA = (BA.min.x + BA.max.x) / 2, sA = (BA.min.z + BA.max.z) / 2;
  t3_object *QA = group(T);
  add(QA, F);
  t3_object_set_position(F, -eA, -BA.min.y, -sA);
  t3_object_set_position(QA, o.x, o.y, o.z);
  rot(QA, 0, o.ry, 0);
  if (o.has_rot) rot(QA, o.rot[0], o.rot[1], o.rot[2]);
  if (BA.max.y - BA.min.y >= 1 && nposts < 40) {
    t3_object_update_matrix_world(QA, true);
    t3_box3 w = box_of(QA);
    const float g = 0.35f;
    posts[nposts++] = (t3_box3){{w.min.x - g, w.min.y, w.min.z - g}, {w.max.x + g, w.max.y + g, w.max.z + g}};
  }
  return QA;
}
#define PROP(n, ...) prop(n, (prop_opt){.shadow = true, __VA_ARGS__})

static void swing_update(upd *u, float Z, float q) {   /* TA(): a robot arm swaying on its base */
  (void)Z;
  t3_object *F = u->a;
  rot(F, F->rotation.x, u->p[0] + sinf(q * u->p[1] + u->p[2]) * u->p[3], F->rotation.z);
}
static void crane_arm_update(upd *u, float PA, float tA) {   /* the two upside-down arms on the ceiling rail */
  (void)PA;
  t3_object *X = u->a;
  if (u->p[0] == 0) { X->position.x = -5 + sinf(tA * 0.5f) * 3.5f; rot(X, X->rotation.x, sinf(tA * 1.2f) * 1.2f, X->rotation.z); }
  else { X->position.x = 5 + sinf(tA * 0.45f + 2) * 3.5f; rot(X, X->rotation.x, cosf(tA * 1.1f) * 1.3f, X->rotation.z); }
}
static void cog_update(upd *u, float Z, float t) {
  (void)t;
  t3_object *F = u->a;
  rot(F, F->rotation.x, F->rotation.y + Z * u->p[0], F->rotation.z);
}
static void piston_update(upd *u, float U, float HA) {
  (void)U;
  t3_object *o = u->a;
  o->scale.y = 1 + 0.18f * sinf(HA * 3 + u->p[0]);
}
static void magnet_update(upd *u, float PA, float tA) {
  (void)PA;
  t3_object *X = u->a;
  rot(X, X->rotation.x, X->rotation.y, sinf(tA * 0.9f) * 0.12f);
  X->position.x = 3.6f + sinf(tA * 0.5f) * 2.2f;
}

/* ---- the coffee bar ---- */
static void coffee_bar(void) {
  t3_object *X = group(T);
  t3_object_set_position(X, -RA + 0.55f, 0, -7.3f); rot(X, 0, PI / 2, 0);
  t3_material *PA = std_mat(5913122, 0.6f, 0), *tA = std_mat(12172996, 0.3f, 0.9f), *iA = std_mat(1908515, 0.6f, 0.4f);
#define CA(geo, mat, x, y, z) ({ t3_mesh *_s = mesh_k(X, geo, mat, x, y, z); shad(_s, true, true); _s; })
  CA(box(4.2f, 1, 0.9f), iA, 0, 0.5f, 0);
  CA(box(4.4f, 0.08f, 1.05f), PA, 0, 1.04f, 0.04f);
  { t3_material *m = std_mat(13116955, 0.35f, 0.6f); CA(box(0.95f, 0.55f, 0.55f), m, -1.2f, 1.35f, 0); t3_release(m); }
  CA(box(0.98f, 0.1f, 0.58f), tA, -1.2f, 1.65f, 0);
  for (int k = -1; k <= 1; k += 2) {
    float Z = 0.25f * k;
    CA(cyl(0.07f, 0.07f, 0.1f, 12), tA, -1.2f + Z, 1.2f, 0.32f);
    T3_OBJ(CA(cyl(0.05f, 0.05f, 0.2f, 12), iA, -1.2f + Z, 1, 0.32f))->visible = false;
  }
  CA(t3_cylinder_geometry_new(0.14f, 0.2f, 0.5f, 16, 1, false), iA, -0.35f, 1.33f, 0.05f);
  CA(t3_cylinder_geometry_new(0.2f, 0.18f, 0.3f, 16, 1, false), tA, 0.2f, 1.23f, 0.1f);
  t3_material *U = std_mat(15524819, 0.5f, 0), *HA = std_mat(15903518, 0.5f, 0);
  for (int Z = 0; Z < 7; Z++) CA(t3_cylinder_geometry_new(0.07f, 0.06f, 0.1f, 12, 1, false), Z % 3 == 0 ? HA : U, 0.7f + Z * 0.16f, 1.13f, 0.2f + (Z % 2) * 0.1f);
  CA(box(3, 0.05f, 0.35f), PA, 0.6f, 2.3f, -0.3f);
  for (int Z = 0; Z < 9; Z++) CA(t3_cylinder_geometry_new(0.065f, 0.055f, 0.1f, 12, 1, false), Z % 2 ? U : HA, -0.7f + Z * 0.3f, 2.38f, -0.3f);
  { t3_material *m = std_mat(14147044, 0.35f, 0.5f); CA(box(1, 1.9f, 0.85f), m, 2.9f, 0.95f, 0); t3_release(m); }
  mesh(X, plane(0.78f, 1.5f), basic_mat(10479871, false), 2.9f, 1, 0.43f);
  for (int Z = 0; Z < 4; Z++) mesh_k(X, box(0.78f, 0.03f, 0.05f), iA, 2.9f, 0.55f + Z * 0.35f, 0.44f);
  static const uint32_t cans[4] = {15903518, 15903518, 6198212, 16734762};
  for (int Z = 0; Z < 12; Z++) mesh(X, cyl(0.035f, 0.035f, 0.12f, 8), basic_mat(cans[Z % 4], true), 2.6f + (Z % 4) * 0.12f + 0.1f, 0.67f + (Z / 4) * 0.35f, 0.43f);
  {
    cv *c = ul_neon(); t3_texture *tex = cv_tex(c, true); cv_free(c);
    t3_material *m = basic_mat(0xffffff, false); t3_material_set_map(m, tex); t3_release(tex);
    m->transparent = true; m->depth_write = false;
    mesh(X, plane(2.9f, 0.9f), m, 0.3f, 3.45f, -0.35f);
  }
  t3_material *stool = std_mat(15903518, 0.5f, 0);
  for (int Z = 0; Z < 3; Z++) {
    t3_object *q = group(X);
    t3_object_set_position(q, -1.2f + Z * 1.1f, 0, 1);
    t3_mesh *oA = mesh_k(q, cyl(0.22f, 0.22f, 0.06f, 16), stool, 0, 0.72f, 0); oA->base.cast_shadow = true;
    mesh_k(q, t3_cylinder_geometry_new(0.03f, 0.04f, 0.7f, 8, 1, false), tA, 0, 0.36f, 0);
  }
  t3_release(stool);
  point_light(X, 16756810, 40, 8, 2, 0.3f, 3, 1);
  t3_release(PA); t3_release(tA); t3_release(iA); t3_release(U); t3_release(HA);
#undef CA
}

/* ---- H(): a workbench desk with two laptops ---- */
static void desk(float X, float PA, float tA) {
  t3_object *iA = group(T);
  t3_object_set_position(iA, X, 0, PA); rot(iA, 0, tA, 0);
  t3_material *cA = std_mat(12159573, 0.55f, 0), *U = std_mat(2237995, 0.5f, 0.6f);
#define HA(geo, mat, x, y, z) ({ t3_mesh *_b = mesh_k(iA, geo, mat, x, y, z); shad(_b, true, true); _b; })
  HA(box(4.4f, 0.08f, 1.1f), cA, 0, 0.78f, 0);
  for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) HA(box(0.08f, 0.76f, 0.08f), U, 2.f * a, 0.38f, 0.45f * b);
  for (int M = -1; M <= 1; M += 2) {
    HA(box(4, 0.06f, 0.34f), cA, 0, 0.46f, (float)M);
    for (int F = -1; F <= 1; F += 2) HA(box(0.06f, 0.44f, 0.06f), U, 1.8f * F, 0.23f, (float)M);
  }
  t3_material *KA = std_mat(2764083, 0.5f, 0.5f);
  static const float lx[2] = {-1.3f, 0.3f};
  for (int k = 0; k < 2; k++) {
    float M = lx[k];
    HA(box(0.42f, 0.02f, 0.3f), KA, M, 0.83f, 0.15f);
    t3_mesh *F = HA(box(0.42f, 0.3f, 0.02f), KA, M, 0.99f, 0);
    rot(F, -0.25f, 0, 0);
    mesh(F, plane(0.38f, 0.26f), basic_mat(6198212, false), 0, 0, 0.012f);
  }
  t3_material *L = std_mat(15524819, 0.5f, 0);
  static const float mx[3] = {-0.6f, 1.2f, 1.7f};
  for (int k = 0; k < 3; k++) HA(t3_cylinder_geometry_new(0.05f, 0.045f, 0.09f, 12, 1, false), L, mx[k], 0.86f, -0.2f);
  { t3_material *m = std_mat(15903518, 0.6f, 0); HA(box(0.5f, 0.04f, 0.35f), m, -1.7f, 0.84f, 0.1f); t3_release(m); }
#undef HA
  t3_release(cA); t3_release(U); t3_release(KA); t3_release(L);
}

/* ---- I(): a shelf unit on the west wall ---- */
static void shelf(float X) {
  t3_object *PA = group(T);
  t3_material *tA = std_mat(2907576, 0.5f, 0.5f), *iA = std_mat(14715418, 0.5f, 0.4f);
  for (int a = -1; a <= 1; a += 2) for (int b = -1; b <= 1; b += 2) {
    t3_mesh *HA = mesh_k(PA, box(0.07f, 2.8f, 0.07f), tA, 1.5f * a, 1.4f, 0.4f * b); HA->base.cast_shadow = true;
  }
  for (int cA = 0; cA < 4; cA++) { t3_mesh *U = mesh_k(PA, box(3.1f, 0.06f, 0.9f), iA, 0, 0.35f + cA * 0.8f, 0); shad(U, true, true); }
  t3_object_set_position(PA, -RA + 0.7f, 0, X); rot(PA, 0, PI / 2, 0);
  t3_release(tA); t3_release(iA);
}

/* ---- the effects of arena_fx_draw ---- */
static t3_object *fxroot;
static t3_object *cones[3];
static unsigned cone_tex, dust_tex;
#define CP 180
static float dust_pos[CP * 3], dust_ph[CP][3];
static t3_light *warm[2], *sun_l;

/* ---- the build ---- */

void arena_scene_setup(t3_renderer *r, t3_scene *scene) {
  S = scene;
  scene->has_background = true; scene->background = t3_color_hex(723983);
  scene->fog = t3_fog_exp2(1447965, 0.011f);
  /* Xr: RoomEnvironment, rendered from the origin into a PMREM at sigma 0.04 */
  t3_scene *env = t3_scene_new();
  env->base.position.y = -3.5f;
  t3_geometry *A = box(1, 1, 1);
  t3_geometry_set_attribute(A, T3_ATTR_UV, NULL);
  t3_material *P = t3_mesh_standard_material_new(0xffffff); P->side = T3_BACK_SIDE;
  t3_material *e = t3_mesh_standard_material_new(0xffffff);
  point_light(env, 16777215, 900, 28, 2, 0.418f, 16.199f, 0.3f);
  t3_mesh *D = t3_mesh_new(A, P);
  t3_object_set_position(D, -0.757f, 13.219f, 0.717f); t3_object_set_scale(D, 31.713f, 28.305f, 28.591f);
  add(env, D); t3_release(D);
  t3_instanced_mesh *w = t3_instanced_mesh_new(A, e, 6);
  static const float inst[6][7] = {
    {-10.906f, 2.009f, 1.846f, -0.195f, 2.328f, 7.905f, 4.651f}, {-5.607f, -0.754f, -0.758f, 0.994f, 1.97f, 1.534f, 3.955f},
    {6.167f, 0.857f, 7.803f, 0.561f, 3.927f, 6.285f, 3.687f}, {-2.017f, 0.018f, 6.124f, 0.333f, 2.002f, 4.566f, 2.064f},
    {2.291f, -0.756f, -2.621f, -0.286f, 1.546f, 1.552f, 1.496f}, {-2.193f, -0.369f, -5.547f, 0.516f, 3.875f, 3.487f, 2.986f}};
  for (int k = 0; k < 6; k++) {
    t3_mat4 m;
    t3_euler eu = {0, inst[k][3], 0, 0};
    t3_mat4_compose(&m, t3_v3(inst[k][0], inst[k][1], inst[k][2]), t3_quat_from_euler(eu), t3_v3(inst[k][4], inst[k][5], inst[k][6]));
    t3_instanced_mesh_set_matrix_at(w, k, &m);
  }
  add(env, w); t3_release(w);
  static const float panels[6][7] = {
    {50, -16.116f, 14.37f, 8.208f, 0.1f, 2.428f, 2.739f}, {50, -16.109f, 18.021f, -8.207f, 0.1f, 2.425f, 2.751f},
    {17, 14.904f, 12.198f, -1.832f, 0.15f, 4.265f, 6.331f}, {43, -0.462f, 8.89f, 14.52f, 4.38f, 5.441f, 0.088f},
    {20, 3.235f, 11.486f, -12.541f, 2.5f, 2, 0.1f}, {100, 0, 20, 0, 1, 0.1f, 1}};
  for (int k = 0; k < 6; k++) {
    t3_material *m = t3_mesh_lambert_material_new(0x000000);
    m->emissive = t3_color_hex(0xffffff); m->emissive_intensity = panels[k][0];
    t3_mesh *i = t3_mesh_new(A, m);
    t3_object_set_position(i, panels[k][1], panels[k][2], panels[k][3]); t3_object_set_scale(i, panels[k][4], panels[k][5], panels[k][6]);
    add(env, i); t3_release(i); t3_release(m);
  }
  t3_release(A); t3_release(P); t3_release(e);
  t3_pmrem_generator *pg = t3_pmrem_generator_new(r);
  t3_render_target *rt = t3_pmrem_from_scene(pg, env, 0.04f, 0.1f, 100);
  t3_scene_set_environment(scene, rt->texture);
  scene->environment_intensity = 0.45f;
  t3_pmrem_generator_destroy(pg);
  t3_release(env);
}

static void build_room(void) {
  T = group(S);
  fxroot = t3_group_new();
  /* the floor */
  { cv *c = ul_floor(); t3_texture *D = zs(c, 5, 4, 8, true); cv_free(c);
    t3_mesh *w = mesh(T, plane(RA * 2, FP * 2), std_map(D, 13619412, 0.82f, 0.05f), 0, 0, 0); t3_release(D);
    rot(w, -PI / 2, 0, 0); w->base.receive_shadow = true; }
  /* the floor decal */
  { cv *c = bl_decal(); t3_texture *tx = zs(c, 1, 1, 8, true); cv_free(c);
    t3_material *m = t3_mesh_standard_material_new(0xffffff); t3_material_set_map(m, tx); t3_release(tx);
    m->transparent = true; m->roughness = 0.7f; m->metalness = 0; m->depth_write = false;
    t3_mesh *g = mesh(T, plane(11.2f, 11.2f), m, 0, 0.012f, 0); rot(g, -PI / 2, 0, 0); g->base.receive_shadow = true; }
  /* the walls */
  cv *wc = ll_wall(); t3_texture *wt = zs(wc, 6, 2.4f, 8, true); cv_free(wc);
  t3_material *wm = std_map(wt, 13620186, 0.6f, 0.5f); t3_release(wt);
  struct { float X, PA, tA, iA, cA, U; } walls[4] = {
    {RA * 2, MP, 0, MP / 2, -FP, 0}, {RA * 2, MP, 0, MP / 2, FP, PI}, {FP * 2, MP, -RA, MP / 2, 0, PI / 2}, {FP * 2, MP, RA, MP / 2, 0, -PI / 2}};
  for (int k = 0; k < 4; k++) {
    t3_material *m = t3_material_clone(wm);   /* n(): a material per wall, sharing the texture */
    t3_mesh *HA = mesh(T, plane(walls[k].X, walls[k].PA), m, walls[k].tA, walls[k].iA, walls[k].cA);
    rot(HA, 0, walls[k].U, 0); HA->base.receive_shadow = true;
  }
  t3_release(wm);
  /* the hazard tape and the skirting along the walls */
  cv *v = lc_hazard();
  struct { float X, PA, tA, iA; } tape[4] = {{RA * 2, 0, -FP + 0.01f, 0}, {RA * 2, 0, FP - 0.01f, PI}, {FP * 2, -RA + 0.01f, 0, PI / 2}, {FP * 2, RA - 0.01f, 0, -PI / 2}};
  for (int k = 0; k < 4; k++) {
    float X = tape[k].X, PA = tape[k].PA, tA = tape[k].tA, iA = tape[k].iA;
    t3_texture *tt = zs(v, X / 0.28f, 1, 8, true);
    t3_material *m = std_map(tt, 0xffffff, 0.7f, 0); t3_release(tt);
    t3_mesh *cA = mesh(T, plane(X, 0.28f), m, PA, 0.5f, tA); rot(cA, 0, iA, 0);
    t3_mesh *U = mesh(T, plane(X, 0.35f), std_mat(2303531, 0.9f, 0), PA, 0.18f, tA + (fabsf(iA) < 0.1f ? 0.005f : 0) * 1);
    rot(U, 0, iA, 0);
    U->base.position.x += sinf(iA) * 0.008f; U->base.position.z += cosf(iA) * 0.008f;
  }
  /* the ceiling, the beams, the duct and the sprinkler pipe */
  { t3_mesh *o = mesh(T, plane(RA * 2, FP * 2), std_mat(1842722, 1, 0), 0, MP, 0); rot(o, PI / 2, 0, 0); }
  t3_material *B = std_mat(3159098, 0.6f, 0.6f);
  for (int X = -2; X <= 2; X++) mesh_k(T, box(0.4f, 0.5f, FP * 2), B, X * 5.2f, MP - 0.25f, 0);
  for (int X = -1; X <= 1; X++) mesh_k(T, box(RA * 2, 0.3f, 0.3f), B, 0, MP - 0.6f, X * 6.f);
  t3_release(B);
  { t3_mesh *z = mesh(T, cyl(0.55f, 0.55f, RA * 2, 20), std_mat(9080982, 0.5f, 0.7f), 0, MP - 1.5f, -FP + 1.5f); rot(z, 0, 0, PI / 2); }
  { t3_mesh *c = mesh(T, cyl(0.16f, 0.16f, RA * 2, 12), std_mat(13116955, 0.5f, 0.4f), 0, MP - 0.9f, -FP + 0.7f); rot(c, 0, 0, PI / 2); }
  /* the two pegboards */
  { cv *c = dl_pegboard(); t3_texture *Q = zs(c, 1, 1, 8, true); cv_free(c);
    t3_material *pm = std_map(Q, 0xffffff, 0.9f, 0); t3_release(Q);
    t3_material *fm = std_mat(2237995, 0.6f, 0.5f);
    for (int k = -1; k <= 1; k += 2) {
      float X = 8.6f * k;
      t3_mesh *PA = mesh_k(T, plane(6.6f, 3.3f), pm, X, 3.3f, -FP + 0.05f); PA->base.receive_shadow = true;
      mesh_k(T, box(6.8f, 3.5f, 0.02f), fm, X, 3.3f, -FP + 0.016f);
    }
    t3_release(pm); t3_release(fm); }
  /* the OPEN LATE sign */
  { cv *c = ll_sign(); t3_texture *tx = zs(c, 1, 1, 8, true); cv_free(c);
    t3_material *m = basic_mat(0xffffff, false); t3_material_set_map(m, tx); t3_release(tx);
    mesh(T, plane(9.2f, 2.88f), m, 0, 3.55f, -FP + 0.07f); }
  /* the roller shutter and its frame */
  { cv *c = xl_shutter(); t3_texture *tx = zs(c, 1, 1, 8, true); cv_free(c);
    t3_mesh *j = mesh(T, plane(7, 5.2f), std_map(tx, 0xffffff, 0.55f, 0.7f), RA - 0.05f, 2.6f, -2); t3_release(tx);
    rot(j, 0, -PI / 2, 0);
    mesh(T, box(0.2f, 5.5f, 7.4f), std_mat(15903518, 0.7f, 0), RA - 0.12f, 2.7f, -2);
    j->base.position.x = RA - 0.15f; }
  /* the high windows, their frames and light cones */
  { cv *c = nl_cone(); uint8_t *px = malloc(64 * 256 * 4); cv_read(c, px); cv_free(c);
    cone_tex = fx_texture(px, 64, 256, false, true); free(px);
    t3_material *fm = std_mat(1776929, 0.6f, 0.6f);
    static const float xs[3] = {-6.5f, 0, 6.5f};
    for (int k = 0; k < 3; k++) {
      float X = xs[k];
      t3_mesh *PA = mesh(T, plane(3.2f, 1.8f), basic_mat(13625087, false), -RA + 0.05f, 6.1f, X); rot(PA, 0, PI / 2, 0);
      mesh_k(T, box(0.12f, 2, 3.4f), fm, -RA + 0.04f, 6.1f, X);
      t3_object *iA = t3_object_new();
      t3_object_set_position(iA, -RA + 3.3f, 3.2f, X + 0.6f); rot(iA, 0, PI / 2, -0.62f);
      add(fxroot, iA); t3_release(iA); cones[k] = iA;
    }
    t3_release(fm); }
}

static void build_props_a(void) {
  for (int k = 0; k < 3; k++) { static const float xs[3] = {-7.4f, -4.3f, 4.3f}; PROP("survival_workbench", .x = xs[k], .z = -FP + 1, .h = 1.25f, .ry = 0); }
  PROP("survival_workbench-anvil", .x = -1.6f, .z = -FP + 1, .h = 1.2f);
  PROP("survival_workbench-grind", .x = 1.6f, .z = -FP + 1, .h = 1.2f);
  PROP("survival_tool-hammer-upgraded", .x = -7.2f, .y = 1.28f, .z = -FP + 1, .s = 1.1f, .has_rot = true, .rot = {0, 0.6f, PI / 2});
  PROP("survival_box-open", .x = -4.4f, .y = 1.25f, .z = -FP + 1, .h = 0.45f);
  static const float sx[4] = {-3.4f, 0, 3.4f, 6.8f};
  for (int k = 0; k < 4; k++) shelf(sx[k]);
  static const struct { const char *n; float h; } h[8] = {{"survival_box", 0.42f}, {"survival_box-large", 0.55f}, {"factory_box-wide", 0.4f},
    {"survival_barrel", 0.7f}, {"survival_bucket", 0.4f}, {"factory_box-long", 0.35f}, {"survival_chest", 0.4f}, {"survival_box-open", 0.4f}};
  int d = 0;
  for (int k = 0; k < 4; k++) for (int PA = 0; PA < 4; PA++) for (int s = -1; s <= 1; s += 2) {
    if (ck_rand() < 0.2f) continue;
    float tA = 0.9f * s;
    int ix = d++ % 8;
    float ry = ck_rand() * 3;
    prop(h[ix].n, (prop_opt){.x = -RA + 0.7f, .y = 0.38f + PA * 0.8f, .z = sx[k] + tA, .ry = ry, .h = h[ix].h, .shadow = false});
  }
}
static void build_props_b(void) {
  PROP("factory_machine-window", .x = RA - 1.6f, .z = 6.6f, .h = 3, .ry = -PI / 2);
  PROP("factory_machine-bed", .x = RA - 1.6f, .z = 8.6f, .h = 2, .ry = -PI / 2);
  PROP("factory_robot-arm-a", .x = RA - 2, .z = -7.6f, .h = 3.2f, .ry = -PI / 2 + 0.4f);
  PROP("factory_hopper-square", .x = RA - 2.2f, .z = 3.2f, .h = 3.6f, .ry = 0);
  prop("factory_screen-wide", (prop_opt){.x = RA - 0.5f, .y = 2.5f, .z = -5.5f, .h = 1.4f, .ry = -PI / 2, .shadow = false});
  PROP("factory_machine", .x = -RA + 2, .z = FP - 2, .h = 2.6f, .ry = PI * 0.8f);
  PROP("factory_robot-arm-b", .x = RA - 2.2f, .z = FP - 2.2f, .h = 3, .ry = PI);
  static const struct { const char *n; float x, z, h; } b[8] = {
    {"factory_cone", -6.2f, 5.8f, 0.7f}, {"factory_cone", 6.4f, -5.8f, 0.7f}, {"factory_cone", -6.4f, -4.2f, 0.7f}, {"factory_cone", 6.2f, 5.2f, 0.7f},
    {"survival_resource-planks", 11.3f, 9, 0.6f}, {"survival_resource-wood", 9.8f, 8, 0.8f}, {"survival_signpost", -8, -8.6f, 2.4f},
    {"factory_warning-orange", 8.2f, -8.4f, 1.4f}};
  for (int k = 0; k < 8; k++) { float ry = ck_rand() * 6; PROP(b[k].n, .x = b[k].x, .z = b[k].z, .h = b[k].h, .ry = ry); }
  prop("factory_crane-lift", (prop_opt){.x = -3.5f, .y = MP - 3.6f, .z = -1.5f, .s = 1.6f, .shadow = false});
  prop("factory_pipe-large-long", (prop_opt){.x = -RA + 0.5f, .y = MP - 2.2f, .z = 0, .s = 2.6f, .ry = PI / 2, .shadow = false});
}

static void build_mid(void) {
  coffee_bar();
  desk(-8.2f, 5.4f, PI / 2); desk(8, -5.2f, PI / 2);
  g0_mega(8.3f, -FP + 1.55f, 6.9f, 0.19f);
  k0_eye(-9.4f, 6.5f, -8.3f, 2.5f);
  /* the posts with their taped feet */
  t3_material *V = std_mat(3817286, 0.5f, 0.7f);
  cv *lc = lc_hazard(); t3_texture *st = zs(lc, 1, 6, 8, true); cv_free(lc);
  t3_material *Sm = std_map(st, 0xffffff, 0.6f, 0); t3_release(st);
  static const float posts[8][2] = {{-RA + 0.3f, -1.7f}, {-RA + 0.3f, 1.7f}, {-RA + 0.3f, 5.1f}, {RA - 0.3f, -8.9f}, {RA - 0.3f, 5.2f}, {RA - 0.3f, 9.4f}, {-RA + 0.3f, 9.4f}, {-RA + 0.3f, -9.4f}};
  for (int k = 0; k < 8; k++) {
    t3_mesh *tA = mesh_k(T, box(0.5f, MP, 0.5f), V, posts[k][0], MP / 2, posts[k][1]); shad(tA, true, true);
    mesh_k(T, box(0.54f, 1.2f, 0.54f), Sm, posts[k][0], 0.6f, posts[k][1]);
  }
  t3_release(V); t3_release(Sm);
  /* the cables strung under the ceiling and along the walls */
  t3_vec3 seg[19][2]; float sag[19]; int n = 0;
  for (int X = 0; X < 9; X++) {
    float PA = -11 + X * 2.7f, tA = ck_rr(-8, 8);
    float x2 = PA + ck_rr(-1.5f, 1.5f), z2 = tA + ck_rr(-2, 2);
    seg[n][0] = t3_v3(PA, MP - 0.4f, tA); seg[n][1] = t3_v3(x2, MP - 0.4f, z2); sag[n++] = ck_rr(1.2f, 3.2f);
  }
  for (int X = 0; X < 5; X++) {
    float PA = -8 + X * 4.f;
    seg[n][0] = t3_v3(-RA + 0.4f, MP - 1, PA); seg[n][1] = t3_v3(-RA + 0.4f, MP - 1, PA + 3.6f); sag[n++] = 0.9f;
    seg[n][0] = t3_v3(RA - 0.4f, MP - 1.4f, PA); seg[n][1] = t3_v3(RA - 0.4f, MP - 1.4f, PA + 3.6f); sag[n++] = 0.8f;
  }
  f0_cables(seg, sag, n);
  qr_screens(RA - 0.3f, 6.9f, -2, -PI / 2, 3, 1, 1.9f, 1.2f);
  qr_screens(-RA + 0.3f, 6.4f, 3.4f, PI / 2, 3, 1, 1.9f, 1.2f);
  qr_screens(0, 6.6f, FP - 0.3f, PI, 4, 1, 2.4f, 1.5f);
  kr_beacon(-RA + 0.6f, 7.3f, -8.8f, 15903518);
  kr_beacon(RA - 0.6f, 7.3f, 7.4f, 13782842);
  kr_beacon(0, 7.6f, -9.6f, 15903518);
  sparks.at = t3_v3(-3.1f, 1.45f, -FP + 2.4f);
  for (int k = 0; k < 140; k++) sparks.e[k * 3 + 1] = -50;
  push_upd(y0_update);
}

static void build_animated(void) {
  /* TA: six robot arms swaying on their bases */
  static const struct { const char *n; float x, z, h, U, HA, KA, L; } ta[6] = {
    {"factory_robot-arm-a", -3.4f, -FP + 2.4f, 4.6f, 0.2f, 0.7f, 0.8f, 0}, {"factory_robot-arm-b", 5, -FP + 3.3f, 4, -0.4f, 0.8f, 0.65f, 1.7f},
    {"factory_robot-arm-a", -RA + 2.4f, -1.8f, 3.6f, PI / 2, 0.9f, 0.9f, 0.5f}, {"factory_robot-arm-b", RA - 2.4f, 0.6f, 3.4f, -PI / 2, 0.9f, 0.75f, 2.4f},
    {"factory_robot-arm-a", -6.4f, 8.6f, 3.6f, PI, 0.9f, 0.7f, 3}, {"factory_robot-arm-b", 6.8f, 8.6f, 3.4f, PI, 0.9f, 0.85f, 0.9f}};
  for (int k = 0; k < 6; k++) {
    t3_object *F = PROP(ta[k].n, .x = ta[k].x, .z = ta[k].z, .h = ta[k].h, .ry = ta[k].U);
    if (!F) continue;
    upd *u = push_upd(swing_update); u->a = F; u->p[0] = ta[k].U; u->p[1] = ta[k].KA; u->p[2] = ta[k].L; u->p[3] = ta[k].HA;
  }
  { t3_object *X = PROP("factory_robot-arm-a", .x = -5, .y = MP - 3.2f, .z = -3, .h = 3.2f, .ry = 0, .has_rot = true, .rot = {PI, 0, 0});
    if (X) { upd *u = push_upd(crane_arm_update); u->a = X; u->p[0] = 0; } }
  { t3_object *X = PROP("factory_robot-arm-b", .x = 5, .y = MP - 3, .z = 3, .h = 3, .ry = 0, .has_rot = true, .rot = {PI, 0, 0});
    if (X) { upd *u = push_upd(crane_arm_update); u->a = X; u->p[0] = 1; } }
  /* aA: the cogs on the walls, each spun about a pivot .11 up */
  static const struct { const char *n; float x, y, z, h, U, HA; } cogs[6] = {
    {"factory_cog-a", -RA + 0.06f, 6.1f, -5.6f, 3.6f, -1, 0.5f}, {"factory_cog-b", -RA + 0.06f, 5.4f, -2.2f, 2.4f, -1, -0.9f},
    {"factory_cog-c", -RA + 0.06f, 6.2f, 8.4f, 3, -1, 0.6f}, {"factory_cog-b", RA - 0.06f, 6.6f, 6.4f, 3.4f, 1, -0.5f},
    {"factory_cog-a", RA - 0.06f, 6.4f, 3.2f, 2.2f, 1, 1}, {"factory_cog-c", RA - 0.06f, 6.2f, -8.6f, 3.2f, 1, 0.55f}};
  for (int k = 0; k < 6; k++) {
    t3_object *KA = PROP(cogs[k].n, .x = cogs[k].x, .y = cogs[k].y, .z = cogs[k].z, .h = cogs[k].h, .has_rot = true, .rot = {0, 0, cogs[k].U * PI / 2});
    if (!KA || KA->child_count < 1) continue;
    t3_object *L = KA->children[0];
    t3_retain(L);
    t3_object_remove(KA, L);
    t3_object *F = group(KA);
    F->position.y = 0.11f;
    L->position.y -= 0.11f;
    add(F, L); t3_release(L);
    upd *u = push_upd(cog_update); u->a = F; u->p[0] = cogs[k].HA;
  }
  static const float pist[3][3] = {{-11.4f, 9, 0}, {11.4f, -9.2f, 1.5f}, {-11.4f, -9.4f, 3}};
  for (int k = 0; k < 3; k++) {
    t3_object *iA = PROP("factory_piston-round", .x = pist[k][0], .z = pist[k][1], .h = 3);
    if (iA) { upd *u = push_upd(piston_update); u->a = iA; u->p[0] = pist[k][2]; }
  }
  { t3_object *X = prop("factory_crane-magnet", (prop_opt){.x = 3.6f, .y = MP - 4.2f, .z = -3.2f, .h = 1.1f, .shadow = false});
    if (X) { upd *u = push_upd(magnet_update); u->a = X; } }
  static const struct { const char *n; float x, z, h; } last[5] = {
    {"factory_box-large", -12, -5.8f, 1.1f}, {"factory_box-wide", -12, -4.2f, 0.9f}, {"survival_barrel", -12.2f, 8.9f, 1},
    {"survival_barrel-open", -11, 9.1f, 1}, {"factory_hopper-square", 11.6f, -5.6f, 3.2f}};
  for (int k = 0; k < 5; k++) { float ry = ck_rand() * 3; PROP(last[k].n, .x = last[k].x, .z = last[k].z, .h = last[k].h, .ry = ry); }
}

static void build_lights(void) {
  /* the hanging lamps */
  t3_material *gP = std_mat(2830133, 0.5f, 0.7f), *AP = basic_mat(16773842, false);
  t3_material *shade = std_mat(1908773, 0.5f, 0.8f); shade->side = T3_DOUBLE_SIDE;
  static const float lamps[5][2] = {{-5.2f, -3.6f}, {5.2f, -3.6f}, {-5.2f, 3.6f}, {5.2f, 3.6f}, {0, 0}};
  for (int k = 0; k < 5; k++) {
    float X = lamps[k][0], PA = lamps[k][1];
    mesh_k(T, t3_cylinder_geometry_new(0.02f, 0.02f, 2.6f, 6, 1, false), gP, X, MP - 1.3f - 0.4f, PA);
    mesh_k(T, t3_cone_geometry_new(0.62f, 0.4f, 24, 1, true), shade, X, MP - 2.8f, PA);
    mesh_k(T, t3_sphere_geometry_new(0.14f, 12, 10), AP, X, MP - 2.95f, PA);
  }
  t3_release(gP); t3_release(AP); t3_release(shade);
  t3_light *hemi = t3_hemisphere_light_new(13621503, 3814184, 0.85f); add(S, hemi); t3_release(hemi);
  t3_light *sun = t3_directional_light_new(16773340, 2.4f);
  t3_object_set_position(sun, -7, 12, 5);
  sun_l = sun;
  t3_object_set_position(sun->target, 0, 0, 0);
  sun->base.cast_shadow = true;
  sun->shadow->map_width = sun->shadow->map_height = 2048;
  t3_camera *DA = sun->shadow->camera;
  DA->left = -8; DA->right = 8; DA->top = 8; DA->bottom = -8; DA->near = 1; DA->far = 34;
  t3_camera_update_projection_matrix(DA);
  sun->shadow->bias = -4e-4f; sun->shadow->normal_bias = 0.03f;
  add(S, sun); add(S, sun->target); t3_release(sun);
  warm[0] = point_light(S, 16767392, 150, 18, 2, -3.2f, MP - 3.1f, 0);
  warm[1] = point_light(S, 16767392, 150, 18, 2, 3.2f, MP - 3.1f, 0);
  point_light(S, 12735532, 150, 15, 2, -6.2f, 3.2f, -2.2f);
  point_light(S, 6198212, 130, 15, 2, 6.2f, 3.2f, -2.2f);
  point_light(S, 15903518, 60, 10, 2, 0, 5.6f, -FP + 2.2f);
  /* the dust */
  for (int X = 0; X < CP; X++) {
    dust_pos[X * 3] = ck_rr(-RA, RA); dust_pos[X * 3 + 1] = ck_rr(0.3f, MP - 1); dust_pos[X * 3 + 2] = ck_rr(-FP, FP);
    dust_ph[X][0] = ck_rand() * 6; dust_ph[X][1] = ck_rand() * 6; dust_ph[X][2] = ck_rr(0.1f, 0.4f);
  }
  cv *c = pl_dot(); uint8_t px[64 * 64 * 4]; cv_read(c, px); cv_free(c);
  dust_tex = fx_texture(px, 64, 64, false, true);
}

/* merge what never moves (batch.c); everything an updater moves stays as built */
static void batch_arena(void) {
  t3_object *skip[128]; int n = 0;
  for (int i = 0; i < 4; i++) if (mega.leds[i]) skip[n++] = T3_OBJ(mega.leds[i]);
  if (eye.group) skip[n++] = eye.group;
  for (int i = 0; i < nbeacons; i++) { if (beacons[i].spin) skip[n++] = beacons[i].spin; if (beacons[i].beam) skip[n++] = beacons[i].beam; }
  for (int i = 0; i < nups && n < 128; i++) {
    void (*f)(upd *, float, float) = ups[i].fn;
    if (f == swing_update || f == crane_arm_update || f == cog_update || f == piston_update || f == magnet_update) skip[n++] = (t3_object *)ups[i].a;
  }
  batch_static(T, skip, n);
}

int arena_build(t3_scene *scene, int stage) {
  if (scene) S = scene;
  switch (stage) {
    case 0: build_room(); return 0;
    case 1: build_props_a(); return 0;
    case 2: build_props_b(); return 0;
    case 3: build_mid(); return 0;
    case 4: build_animated(); return 0;
    default: build_lights(); batch_arena(); return 1;
  }
}

void arena_set_focus(float x, float y, float z) { focus = t3_v3(x, y, z); }
/* the menus' light: the sun from in front of the camera and low, so a fighter facing it
 * is lit across the face (the page's sun, up on the left, leaves KEYHOLDER's cel faces half dark) */
void arena_menu_light(bool on) {
  if (!sun_l) return;
  if (on) t3_object_set_position(sun_l, 0, 3.2f, 11); else t3_object_set_position(sun_l, -7, 12, 5);
}

void arena_update(float X, float PA) {
  for (int i = 0; i < nups; i++) ups[i].fn(&ups[i], X, PA);
  for (int tA = 0; tA < CP; tA++) {
    float *iA = dust_ph[tA];
    dust_pos[tA * 3] += sinf(PA * 0.3f + iA[0]) * 0.12f * X;
    dust_pos[tA * 3 + 1] += sinf(PA * 0.4f + iA[1]) * 0.06f * X;
    dust_pos[tA * 3 + 2] += cosf(PA * 0.25f + iA[0]) * 0.1f * X;
  }
  for (int iA = 0; iA < 2; iA++) if (warm[iA]) warm[iA]->intensity = 150 * (1 + 0.03f * sinf(PA * 37 + iA * 3) * sinf(PA * 5.3f + iA));
}

static void plane_tris(float w, float h, float *xyz, float *uv) {
  /* PlaneGeometry(w, h): (-w/2, h/2) top left; the uvs flipped for a flipY texture */
  float x0 = -w / 2, x1 = w / 2, y0 = -h / 2, y1 = h / 2;
  float P[4][3] = {{x0, y1, 0}, {x1, y1, 0}, {x0, y0, 0}, {x1, y0, 0}};
  float U[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
  static const int idx[6] = {0, 2, 1, 2, 3, 1};
  for (int k = 0; k < 6; k++) {
    memcpy(xyz + k * 3, P[idx[k]], sizeof(float) * 3);
    if (uv) memcpy(uv + k * 2, U[idx[k]], sizeof(float) * 2);
  }
}

void arena_fx_draw(void) {
  if (!fxroot) return;
  t3_object_update_matrix_world(fxroot, true);
  for (int b = 0; b < nbeacons; b++) t3_object_update_world_matrix(beacons[b].beam, true, false);
  float xyz[18], uv[12];
  /* the beacon beams: RP(6, .5), MeshBasic colour w, opacity .12, additive, both sides, no depth write */
  plane_tris(6, 0.5f, xyz, NULL);
  for (int b = 0; b < nbeacons; b++) {
    float col[4] = {beacons[b].col[0], beacons[b].col[1], beacons[b].col[2], 0.12f};
    fx_tris(xyz, NULL, NULL, 6, beacons[b].beam->matrix_world.e, 0, col, FXB_ADD,
            FXF_FOG | FXF_TONEMAP | FXF_ENCODE | FXF_DEPTH_TEST, true);
  }
  /* the window light cones: RP(2.6, 10), the Nl texture (no colour space), opacity .16, additive */
  plane_tris(2.6f, 10, xyz, uv);
  for (int k = 0; k < 3; k++) {
    if (!cones[k]) continue;
    float col[4] = {1, 1, 1, 0.16f};
    fx_tris(xyz, uv, NULL, 6, cones[k]->matrix_world.e, cone_tex, col, FXB_ADD,
            FXF_FOG | FXF_TONEMAP | FXF_ENCODE | FXF_DEPTH_TEST | FXF_TEX_LINEAR, true);
  }
  /* the sparks: PointsMaterial size .07, 16760928, opacity .95, additive, square points */
  { float col[4]; fx_hex_linear(16760928, col); col[3] = 0.95f;
    fx_points(sparks.e, NULL, NULL, 140, 0.07f, CK_H / 2.f, FXP_SQUARE, 0, col, FXB_ADD, FXF_FOG | FXF_TONEMAP | FXF_ENCODE | FXF_DEPTH_TEST); }
  /* the dust: size .06, the pl dot, opacity .35, additive, 16773336 */
  { float col[4]; fx_hex_linear(16773336, col); col[3] = 0.35f;
    fx_points(dust_pos, NULL, NULL, CP, 0.06f, CK_H / 2.f, FXP_TEX, dust_tex, col, FXB_ADD,
              FXF_FOG | FXF_TONEMAP | FXF_ENCODE | FXF_DEPTH_TEST | FXF_TEX_LINEAR); }
}

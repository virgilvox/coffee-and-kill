/* overlay.c: see overlay.h. One small GL program: a quad through an affine
 * matrix, sampling a premultiplied RGBA8 texture (or a solid colour), blended
 * ONE, ONE_MINUS_SRC_ALPHA into a non-sRGB framebuffer, so the blend runs on
 * encoded values as the browser's compositor does. */
#include "overlay.h"
#include "ck.h"
#include "wasmcart.h"
#include "wc_gl.h"
#ifndef GL_UNPACK_ALIGNMENT
#define GL_UNPACK_ALIGNMENT 0x0CF5
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>

static GLuint prog, vao, vbo;
static GLint u_m, u_size, u_tex, u_mode, u_col, u_alpha, u_bright, u_uv;
static ov_tex *grain_tex;
static uint8_t *upbuf; static size_t upcap;

static const char *VS =
  "#version 300 es\n"
  "layout(location=0) in vec2 p;\n"
  "uniform mat3 m; uniform vec2 size; uniform vec4 uv;\n"
  "out vec2 vuv; out vec2 vpx;\n"
  "void main(){ vec2 q = p * size; vec3 s = m * vec3(q, 1.0);\n"
  "  vuv = uv.xy + p * uv.zw; vpx = s.xy;\n"
  "  gl_Position = vec4(s.x / 640.0 - 1.0, 1.0 - s.y / 360.0, 0.0, 1.0); }\n";
static const char *FS =
  "#version 300 es\nprecision highp float;\n"
  "in vec2 vuv; in vec2 vpx; out vec4 o;\n"
  "uniform sampler2D tex; uniform int mode; uniform vec4 col; uniform float alpha; uniform float bright;\n"
  "void main(){\n"
  "  if (mode == 0) { vec4 c = texture(tex, vuv); c.rgb = min(c.rgb * bright, vec3(c.a)); o = c * alpha; }\n"
  "  else if (mode == 1) { o = vec4(col.rgb * col.a, col.a); }\n"
  "  else {\n"
  /* radial-gradient(ellipse at center, transparent 55%, rgba(0,0,0,.55) 100%): farthest-corner ellipse */
  "    vec2 d = (vpx - vec2(640.0, 360.0)) / (vec2(640.0, 360.0) * 1.41421356);\n"
  "    float t = clamp((length(d) - 0.55) / 0.45, 0.0, 1.0);\n"
  "    o = vec4(0.0, 0.0, 0.0, 0.55 * t); }\n"
  "}\n";

static GLuint sh(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL); glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log); ck_log("overlay shader: %s", log); }
  return s;
}

/* ---- the grain: SVG feTurbulence (type fractalNoise, baseFrequency .9,
 * numOctaves 2, stitchTiles, seed 0) through the page's feColorMatrix (white,
 * alpha * .9), 160 px, following the reference code of the filter spec ---- */
#define BSize 0x100
#define BM 0xff
#define PerlinN 0x1000
static int lat[BSize + BSize + 2];
static double grad[4][BSize + BSize + 2][2];
typedef struct { int w, h, wrapx, wrapy; } stitch_t;
static long seed_rand(long s) { s = 16807 * (s % 127773) - 2836 * (s / 127773); if (s <= 0) s += 2147483647; return s; }
static void turb_init(long seed) {
  double s; int i, j, k;
  if (seed <= 0) seed = -(seed % (2147483647 - 1)) + 1;
  if (seed > 2147483647 - 1) seed = 2147483647 - 1;
  for (k = 0; k < 4; k++) for (i = 0; i < BSize; i++) {
    lat[i] = i;
    for (j = 0; j < 2; j++) grad[k][i][j] = (double)(((seed = seed_rand(seed)) % (BSize + BSize)) - BSize) / BSize;
    s = sqrt(grad[k][i][0] * grad[k][i][0] + grad[k][i][1] * grad[k][i][1]);
    if (s > 0) { grad[k][i][0] /= s; grad[k][i][1] /= s; }
  }
  while (--i) { k = lat[i]; lat[i] = lat[j = (seed = seed_rand(seed)) % BSize]; lat[j] = k; }
  for (i = 0; i < BSize + 2; i++) {
    lat[BSize + i] = lat[i];
    for (k = 0; k < 4; k++) for (j = 0; j < 2; j++) grad[k][BSize + i][j] = grad[k][i][j];
  }
}
#define s_curve(t) ((t) * (t) * (3. - 2. * (t)))
#define lerpd(t, a, b) ((a) + (t) * ((b) - (a)))
static double noise2(int ch, double vx, double vy, stitch_t *st) {
  int bx0, bx1, by0, by1, b00, b10, b01, b11; double rx0, rx1, ry0, ry1, *q, sx, sy, a, b, t, u, v; int i, j;
  t = vx + PerlinN; bx0 = ((int)t) & BM; bx1 = (bx0 + 1) & BM; rx0 = t - (int)t; rx1 = rx0 - 1.0;
  t = vy + PerlinN; by0 = ((int)t) & BM; by1 = (by0 + 1) & BM; ry0 = t - (int)t; ry1 = ry0 - 1.0;
  if (st) {
    if (bx0 >= st->wrapx) bx0 -= st->w;
    if (bx1 >= st->wrapx) bx1 -= st->w;
    if (by0 >= st->wrapy) by0 -= st->h;
    if (by1 >= st->wrapy) by1 -= st->h;
  }
  bx0 &= BM; bx1 &= BM; by0 &= BM; by1 &= BM;
  i = lat[bx0]; j = lat[bx1];
  b00 = lat[i + by0]; b10 = lat[j + by0]; b01 = lat[i + by1]; b11 = lat[j + by1];
  sx = s_curve(rx0); sy = s_curve(ry0);
  q = grad[ch][b00]; u = rx0 * q[0] + ry0 * q[1];
  q = grad[ch][b10]; v = rx1 * q[0] + ry0 * q[1];
  a = lerpd(sx, u, v);
  q = grad[ch][b01]; u = rx0 * q[0] + ry1 * q[1];
  q = grad[ch][b11]; v = rx1 * q[0] + ry1 * q[1];
  b = lerpd(sx, u, v);
  return lerpd(sy, a, b);
}
static double turbulence(int ch, double px, double py, double bf, int oct, int tw, int th) {
  stitch_t st, *pst = &st;
  double fx = bf, fy = bf;
  /* stitchTiles: adjust the frequencies so the tile repeats */
  double lo = floor(tw * fx) / tw, hi = ceil(tw * fx) / tw;
  fx = (fx / lo < hi / fx) ? lo : hi;
  lo = floor(th * fy) / th; hi = ceil(th * fy) / th;
  fy = (fy / lo < hi / fy) ? lo : hi;
  st.w = (int)(tw * fx + 0.5); st.wrapx = (int)(0 * fx + PerlinN + st.w);
  st.h = (int)(th * fy + 0.5); st.wrapy = (int)(0 * fy + PerlinN + st.h);
  double sum = 0, ratio = 1, vx = px * fx, vy = py * fy;
  for (int n = 0; n < oct; n++) {
    sum += noise2(ch, vx, vy, pst) / ratio;
    vx *= 2; vy *= 2; ratio *= 2;
    st.w *= 2; st.wrapx = 2 * st.wrapx - PerlinN; st.h *= 2; st.wrapy = 2 * st.wrapy - PerlinN;
  }
  return sum;
}
static void make_grain(void) {
  int n = 160;
  uint8_t *px = malloc((size_t)n * n * 4);
  turb_init(0);
  for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
    double a = (turbulence(3, x + 0.5, y + 0.5, 0.9, 2, n, n) + 1) / 2;   /* fractalNoise: (sum + 1) / 2 */
    a = a < 0 ? 0 : a > 1 ? 1 : a;
    a *= 0.9;                                                           /* feColorMatrix alpha row */
    uint8_t A = (uint8_t)(a * 255 + 0.5);
    uint8_t *p = px + ((size_t)y * n + x) * 4;
    p[0] = p[1] = p[2] = A; p[3] = A;   /* white, premultiplied */
  }
  grain_tex = ov_tex_new(n, n);
  glBindTexture(GL_TEXTURE_2D, grain_tex->id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  free(px);
}

void ov_init(void) {
  prog = glCreateProgram();
  glAttachShader(prog, sh(GL_VERTEX_SHADER, VS));
  glAttachShader(prog, sh(GL_FRAGMENT_SHADER, FS));
  glLinkProgram(prog);
  u_m = glGetUniformLocation(prog, "m"); u_size = glGetUniformLocation(prog, "size");
  u_tex = glGetUniformLocation(prog, "tex"); u_mode = glGetUniformLocation(prog, "mode");
  u_col = glGetUniformLocation(prog, "col"); u_alpha = glGetUniformLocation(prog, "alpha");
  u_bright = glGetUniformLocation(prog, "bright"); u_uv = glGetUniformLocation(prog, "uv");
  static const float q[] = {0, 0, 1, 0, 0, 1, 1, 1};
  glGenVertexArrays(1, &vao); glBindVertexArray(vao);
  glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glBindVertexArray(0);
  make_grain();
}

ov_tex *ov_tex_new(int w, int h) {
  ov_tex *t = calloc(1, sizeof *t);
  t->w = w; t->h = h;
  glGenTextures(1, &t->id);
  glBindTexture(GL_TEXTURE_2D, t->id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  return t;
}
void ov_tex_free(ov_tex *t) { if (!t) return; glDeleteTextures(1, &t->id); free(t); }

static uint8_t *buf(size_t n) {
  if (n > upcap) { upcap = n; upbuf = realloc(upbuf, n); }
  return upbuf;
}
void ov_tex_from_cv_rect(ov_tex *t, cv *c, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  uint8_t *p = buf((size_t)w * h * 4);
  cv_read_rect(c, p, x, y, w, h);
  for (size_t i = 0, n = (size_t)w * h; i < n; i++) {   /* premultiply for the ONE, 1-a blend */
    uint8_t *q = p + i * 4; unsigned a = q[3];
    q[0] = (uint8_t)((q[0] * a + 127) / 255); q[1] = (uint8_t)((q[1] * a + 127) / 255); q[2] = (uint8_t)((q[2] * a + 127) / 255);
  }
  glBindTexture(GL_TEXTURE_2D, t->id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p);
}
void ov_tex_from_cv(ov_tex *t, cv *c) { ov_tex_from_cv_rect(t, c, 0, 0, cv_w(c) < t->w ? cv_w(c) : t->w, cv_h(c) < t->h ? cv_h(c) : t->h); }

void ov_begin(void) {
  glUseProgram(prog);
  glBindVertexArray(vao);
  glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
  glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glViewport(0, 0, CK_W, CK_H);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(u_tex, 0);
}
void ov_end(void) {
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
}

static void set_m(const float m[6]) {
  float g[9] = {m[0], m[1], 0, m[2], m[3], 0, m[4], m[5], 1};
  glUniformMatrix3fv(u_m, 1, GL_FALSE, g);
}

void ov_draw_m(ov_tex *t, const float m[6], float alpha, float bright) {
  if (!t || alpha <= 0) return;
  glBindTexture(GL_TEXTURE_2D, t->id);
  glUniform1i(u_mode, 0); glUniform1f(u_alpha, alpha); glUniform1f(u_bright, bright);
  glUniform2f(u_size, (float)t->w, (float)t->h);
  glUniform4f(u_uv, 0, 0, 1, 1);
  set_m(m);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
void ov_draw(ov_tex *t, float x, float y, float alpha) {
  float m[6] = {1, 0, 0, 1, x, y};
  ov_draw_m(t, m, alpha, 1);
}
void ov_draw_sub(ov_tex *t, int sx, int sy, int sw, int sh, float x, float y, float alpha) {
  if (!t || alpha <= 0) return;
  glBindTexture(GL_TEXTURE_2D, t->id);
  glUniform1i(u_mode, 0); glUniform1f(u_alpha, alpha); glUniform1f(u_bright, 1);
  glUniform2f(u_size, (float)sw, (float)sh);
  glUniform4f(u_uv, (float)sx / t->w, (float)sy / t->h, (float)sw / t->w, (float)sh / t->h);
  float m[6] = {1, 0, 0, 1, x, y};
  set_m(m);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
void ov_fill(float x, float y, float w, float h, uint32_t rgb, float a) {
  if (a <= 0) return;
  glUniform1i(u_mode, 1);
  glUniform4f(u_col, ((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a);
  glUniform2f(u_size, w, h);
  float m[6] = {1, 0, 0, 1, x, y};
  set_m(m);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
void ov_vignette(void) {
  glUniform1i(u_mode, 2);
  glUniform2f(u_size, CK_W, CK_H);
  float m[6] = {1, 0, 0, 1, 0, 0};
  set_m(m);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* #grain: inset -50%, 200% of the viewport, opacity .07, a 160 px tile, moved by
 * @keyframes grain over 1.2 s with steps(6) per keyframe interval */
void ov_grain(double t) {
  static const float kx[6] = {0, -3, 2, -2, 3, 0}, ky[6] = {0, 2, -3, -1, 3, 0};
  double ph = fmod(t, 1.2) / 1.2 * 5.0;           /* five intervals */
  int seg = (int)ph; if (seg > 4) seg = 4;
  double local = ph - seg;
  double k = floor(local * 6) / 6;                 /* steps(6), jump-end */
  float tx = (float)(kx[seg] + (kx[seg + 1] - kx[seg]) * k) / 100.f * (CK_W * 2);
  float ty = (float)(ky[seg] + (ky[seg + 1] - ky[seg]) * k) / 100.f * (CK_H * 2);
  /* the element's top left sits at (-640, -360) plus the translate; the tile repeats from there */
  float ox = -CK_W / 2.f + tx, oy = -CK_H / 2.f + ty;
  glBindTexture(GL_TEXTURE_2D, grain_tex->id);
  glUniform1i(u_mode, 0); glUniform1f(u_alpha, 0.07f); glUniform1f(u_bright, 1);
  glUniform2f(u_size, CK_W, CK_H);
  glUniform4f(u_uv, -ox / 160.f, -oy / 160.f, CK_W / 160.f, CK_H / 160.f);
  float m[6] = {1, 0, 0, 1, 0, 0};
  set_m(m);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void ov_matrix(float m[6], float x, float y, float ox, float oy, float rot, float sx, float sy, float tx, float ty) {
  /* translate(x + ox + tx, y + oy + ty) rotate(rot) scale(sx, sy) translate(-ox, -oy) */
  float c = cosf(rot), s = sinf(rot);
  m[0] = c * sx; m[1] = s * sx; m[2] = -s * sy; m[3] = c * sy;
  m[4] = x + ox + tx - (m[0] * ox + m[2] * oy);
  m[5] = y + oy + ty - (m[1] * ox + m[3] * oy);
}
void ov_matrix_skew(float m[6], float x, float y, float ox, float oy, float skew_x) {
  float k = tanf(skew_x);
  m[0] = 1; m[1] = 0; m[2] = k; m[3] = 1;
  m[4] = x + ox - (ox + k * oy); m[5] = y;
}

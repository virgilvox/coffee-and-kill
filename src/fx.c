/* fx.c: see fx.h. One program, a streaming vertex buffer, three.js's fog,
 * ACESFilmic and sRGB output chunks. */
#include "fx.h"
#include "ck.h"
#include "wasmcart.h"
#include "wc_gl.h"
#ifndef GL_UNPACK_ALIGNMENT
#define GL_UNPACK_ALIGNMENT 0x0CF5
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>

static GLuint prog, vao, vbo, white_tex;
static GLint u_vp, u_model, u_col, u_flags, u_shape, u_scale, u_fogc, u_fogd, u_expo, u_tex, u_points, u_size0;
static float VIEW[16], PROJ[16], VP[16];
static float fog_d, fog_c[3], expo = 1;
static float cam_right[3], cam_up[3];

/* x y z | u v | r g b a | size */
#define STRIDE 10
static float *vb; static int vcap;

static const char *VS =
  "#version 300 es\nprecision highp float; precision highp int;\n"
  "layout(location=0) in vec3 p; layout(location=1) in vec2 uv; layout(location=2) in vec4 c; layout(location=3) in float sz;\n"
  "uniform mat4 vp; uniform mat4 model; uniform int points; uniform float scale; uniform float size0;\n"
  "out vec2 vuv; out vec4 vc; out float vz;\n"
  "void main(){ vec4 w = model * vec4(p, 1.0); vec4 c4 = vp * w; gl_Position = c4;\n"
  "  vuv = uv; vc = c; vz = c4.w;\n"
  "  if (points == 1) gl_PointSize = (sz > 0.0 ? sz : size0) * scale / max(0.1, c4.w); }\n";
static const char *FS =
  "#version 300 es\nprecision highp float; precision highp int;\n"
  "in vec2 vuv; in vec4 vc; in float vz; out vec4 o;\n"
  "uniform sampler2D tex; uniform vec4 col; uniform int flags; uniform int shape; uniform int points;\n"
  "uniform vec3 fogc; uniform float fogd; uniform float expo;\n"
  "vec3 RRTAndODTFit(vec3 v){ vec3 a = v*(v+0.0245786)-0.000090537; vec3 b = v*(0.983729*v+0.4329510)+0.238081; return a/b; }\n"
  "vec3 aces(vec3 color){\n"
  "  const mat3 ACESInputMat = mat3(vec3(0.59719,0.07600,0.02840), vec3(0.35458,0.90834,0.13383), vec3(0.04823,0.01566,0.83777));\n"
  "  const mat3 ACESOutputMat = mat3(vec3(1.60475,-0.10208,-0.00327), vec3(-0.53108,1.10813,-0.07276), vec3(-0.07367,-0.00605,1.07602));\n"
  "  color *= expo / 0.6; color = ACESInputMat * color; color = RRTAndODTFit(color); color = ACESOutputMat * color; return clamp(color, 0.0, 1.0); }\n"
  "vec3 srgb(vec3 v){ return mix(pow(v, vec3(0.41666)) * 1.055 - vec3(0.055), v * 12.92, vec3(lessThanEqual(v, vec3(0.0031308)))); }\n"
  "vec3 lin(vec3 v){ return mix(pow(v * 0.9478672986 + vec3(0.0521327014), vec3(2.4)), v * 0.0773993808, vec3(lessThanEqual(v, vec3(0.04045)))); }\n"
  "void main(){\n"
  "  vec4 c = vc * col; vec2 t = vuv;\n"
  "  if (points == 1) t = vec2(gl_PointCoord.x, 1.0 - gl_PointCoord.y);\n"
  "  if (points == 1 && shape == 2) {\n"   /* the page's particle shader ($r) */
  "    vec2 d = gl_PointCoord - 0.5; float r = length(d) * 2.0; if (r > 1.0) discard;\n"
  "    float a = smoothstep(1.0, 0.0, r); o = vec4(vc.rgb * (0.6 + 0.8 * a), vc.a * a); return; }\n"
  "  if (points == 0 || shape == 1) { vec4 s = texture(tex, t); if ((flags & 32) == 0) s.rgb = lin(s.rgb); c *= s; }\n"
  "  if ((flags & 2) != 0) c.rgb = aces(c.rgb);\n"
  "  if ((flags & 4) != 0) c.rgb = srgb(max(c.rgb, 0.0));\n"
  "  if ((flags & 1) != 0) { float f = 1.0 - exp(-fogd * fogd * vz * vz); c.rgb = mix(c.rgb, fogc, clamp(f, 0.0, 1.0)); }\n"
  "  o = c; }\n";

static GLuint sh(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL); glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, NULL, log); ck_log("fx shader: %s", log); }
  return s;
}

void fx_init(void) {
  prog = glCreateProgram();
  glAttachShader(prog, sh(GL_VERTEX_SHADER, VS));
  glAttachShader(prog, sh(GL_FRAGMENT_SHADER, FS));
  glLinkProgram(prog);
  u_vp = glGetUniformLocation(prog, "vp"); u_model = glGetUniformLocation(prog, "model");
  u_col = glGetUniformLocation(prog, "col"); u_flags = glGetUniformLocation(prog, "flags");
  u_shape = glGetUniformLocation(prog, "shape"); u_scale = glGetUniformLocation(prog, "scale");
  u_fogc = glGetUniformLocation(prog, "fogc"); u_fogd = glGetUniformLocation(prog, "fogd");
  u_expo = glGetUniformLocation(prog, "expo"); u_tex = glGetUniformLocation(prog, "tex");
  u_points = glGetUniformLocation(prog, "points"); u_size0 = glGetUniformLocation(prog, "size0");
  glGenVertexArrays(1, &vao); glBindVertexArray(vao);
  glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, STRIDE * 4, (void *)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, STRIDE * 4, (void *)12);
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, STRIDE * 4, (void *)20);
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, STRIDE * 4, (void *)36);
  glBindVertexArray(0);
  static const uint8_t w[4] = {255, 255, 255, 255};
  white_tex = fx_texture(w, 1, 1, false, false);
}

unsigned fx_texture(const uint8_t *rgba, int w, int h, bool repeat, bool mip) {
  GLuint t; glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  if (mip) glGenerateMipmap(GL_TEXTURE_2D);
  return t;
}

static float s2l(float c) { return c < 0.04045f ? c * 0.0773993808f : powf(c * 0.9478672986f + 0.0521327014f, 2.4f); }
void fx_hex_linear(uint32_t hex, float out[3]) {
  out[0] = s2l(((hex >> 16) & 255) / 255.f); out[1] = s2l(((hex >> 8) & 255) / 255.f); out[2] = s2l((hex & 255) / 255.f);
}
void fx_hex_raw(uint32_t hex, float out[3]) {
  out[0] = ((hex >> 16) & 255) / 255.f; out[1] = ((hex >> 8) & 255) / 255.f; out[2] = (hex & 255) / 255.f;
}

static void mul4(float *o, const float *a, const float *b) {
  float r[16];
  for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
    float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
    r[c * 4 + rr] = s;
  }
  memcpy(o, r, sizeof r);
}
static void inverse_rt(float *o, const float *m) {   /* the inverse of a rigid transform (the camera's matrix_world) */
  float r[16] = {m[0], m[4], m[8], 0, m[1], m[5], m[9], 0, m[2], m[6], m[10], 0, 0, 0, 0, 1};
  float sx = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]); if (sx == 0) sx = 1;
  for (int i = 0; i < 12; i++) if ((i & 3) != 3) r[i] /= sx * sx;
  r[12] = -(r[0] * m[12] + r[4] * m[13] + r[8] * m[14]);
  r[13] = -(r[1] * m[12] + r[5] * m[13] + r[9] * m[14]);
  r[14] = -(r[2] * m[12] + r[6] * m[13] + r[10] * m[14]);
  memcpy(o, r, sizeof r);
}

void fx_frame(t3_camera *cam, float fog_density, uint32_t fog_hex, float exposure) {
  t3_object *o = (t3_object *)cam;
  t3_object_update_matrix_world(o, true);
  inverse_rt(VIEW, o->matrix_world.e);
  memcpy(PROJ, ((t3_camera *)cam)->projection_matrix.e, sizeof PROJ);
  mul4(VP, PROJ, VIEW);
  cam_right[0] = o->matrix_world.e[0]; cam_right[1] = o->matrix_world.e[1]; cam_right[2] = o->matrix_world.e[2];
  cam_up[0] = o->matrix_world.e[4]; cam_up[1] = o->matrix_world.e[5]; cam_up[2] = o->matrix_world.e[6];
  fog_d = fog_density; fx_hex_linear(fog_hex, fog_c); expo = exposure;
}
const float *fx_view(void) { return VIEW; }
const float *fx_proj(void) { return PROJ; }
void fx_project(const float w[3], float *sx, float *sy) {
  float x = VP[0] * w[0] + VP[4] * w[1] + VP[8] * w[2] + VP[12];
  float y = VP[1] * w[0] + VP[5] * w[1] + VP[9] * w[2] + VP[13];
  float ww = VP[3] * w[0] + VP[7] * w[1] + VP[11] * w[2] + VP[15];
  if (ww == 0) ww = 1e-6f;
  *sx = (x / ww * 0.5f + 0.5f) * CK_W;
  *sy = (-y / ww * 0.5f + 0.5f) * CK_H;
}

void fx_begin(void) {
  glUseProgram(prog);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glUniformMatrix4fv(u_vp, 1, GL_FALSE, VP);
  glUniform3fv(u_fogc, 1, fog_c); glUniform1f(u_fogd, fog_d); glUniform1f(u_expo, expo);
  glUniform1i(u_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
  glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
  glViewport(0, 0, CK_W, CK_H);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}
void fx_end(void) {
  glBindVertexArray(0);
  glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
}

static void state(int blend, int flags, bool two_sided) {
  if (blend == FXB_ADD) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_SRC_ALPHA, GL_ONE);
  else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  if (flags & FXF_DEPTH_TEST) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); } else glDisable(GL_DEPTH_TEST);
  glDepthMask((flags & FXF_DEPTH_WRITE) ? GL_TRUE : GL_FALSE);
  if (two_sided) glDisable(GL_CULL_FACE); else { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); }
  glUniform1i(u_flags, flags);
}
static float *room(int n) {
  if (n * STRIDE > vcap) { vcap = n * STRIDE * 2; vb = realloc(vb, sizeof(float) * vcap); }
  return vb;
}
static const float ID[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void fx_tris(const float *xyz, const float *uv, const float *rgba, int n, const float *model,
             unsigned tex, const float col[4], int blend, int flags, bool two_sided) {
  if (n <= 0) return;
  float *v = room(n);
  for (int i = 0; i < n; i++) {
    float *d = v + i * STRIDE;
    d[0] = xyz[i * 3]; d[1] = xyz[i * 3 + 1]; d[2] = xyz[i * 3 + 2];
    d[3] = uv ? uv[i * 2] : 0; d[4] = uv ? uv[i * 2 + 1] : 0;
    if (rgba) { d[5] = rgba[i * 4]; d[6] = rgba[i * 4 + 1]; d[7] = rgba[i * 4 + 2]; d[8] = rgba[i * 4 + 3]; }
    else d[5] = d[6] = d[7] = d[8] = 1;
    d[9] = 0;
  }
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * n * STRIDE, v, GL_STREAM_DRAW);
  state(blend, flags, two_sided);
  glUniformMatrix4fv(u_model, 1, GL_FALSE, model ? model : ID);
  glUniform4fv(u_col, 1, col);
  glUniform1i(u_points, 0); glUniform1i(u_shape, 0);
  glBindTexture(GL_TEXTURE_2D, tex ? tex : white_tex);
  if (!tex) glUniform1i(u_flags, flags | FXF_TEX_LINEAR);
  glDrawArrays(GL_TRIANGLES, 0, n);
}

void fx_points(const float *xyz, const float *rgba, const float *size, int n, float size0, float scale,
               int shape, unsigned tex, const float col[4], int blend, int flags) {
  if (n <= 0) return;
  float *v = room(n);
  for (int i = 0; i < n; i++) {
    float *d = v + i * STRIDE;
    d[0] = xyz[i * 3]; d[1] = xyz[i * 3 + 1]; d[2] = xyz[i * 3 + 2];
    d[3] = d[4] = 0;
    if (rgba) { d[5] = rgba[i * 4]; d[6] = rgba[i * 4 + 1]; d[7] = rgba[i * 4 + 2]; d[8] = rgba[i * 4 + 3]; }
    else d[5] = d[6] = d[7] = d[8] = 1;
    d[9] = size ? size[i] : 0;
  }
  glBufferData(GL_ARRAY_BUFFER, sizeof(float) * n * STRIDE, v, GL_STREAM_DRAW);
  state(blend, flags, true);
  glUniformMatrix4fv(u_model, 1, GL_FALSE, ID);
  glUniform4fv(u_col, 1, col);
  glUniform1i(u_points, 1); glUniform1i(u_shape, shape);
  glUniform1f(u_scale, scale); glUniform1f(u_size0, size0);
  glBindTexture(GL_TEXTURE_2D, tex ? tex : white_tex);
  glDrawArrays(GL_POINTS, 0, n);
}

void fx_sprite(const float p[3], float size, float rot, unsigned tex, const float col[4], int blend, int flags) {
  /* SpriteMaterial: a camera facing quad of `size`, rotated in screen space */
  float c = cosf(rot), s = sinf(rot), h = size / 2;
  float corner[4][2] = {{-h, -h}, {h, -h}, {h, h}, {-h, h}};
  float uvq[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  float xyz[18], uv[12];
  static const int idx[6] = {0, 1, 2, 0, 2, 3};
  for (int k = 0; k < 6; k++) {
    int i = idx[k];
    float x = corner[i][0] * c - corner[i][1] * s, y = corner[i][0] * s + corner[i][1] * c;
    for (int a = 0; a < 3; a++) xyz[k * 3 + a] = p[a] + cam_right[a] * x + cam_up[a] * y;
    uv[k * 2] = uvq[i][0]; uv[k * 2 + 1] = 1 - uvq[i][1];   /* the canvas texture's flipY */
  }
  fx_tris(xyz, uv, NULL, 6, NULL, tex, col, blend, flags, true);
}

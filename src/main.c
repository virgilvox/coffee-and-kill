/* main.c: the wasmcart glue. wc_info, init, the frame: input, the staged
 * boot (the page's loading screen), the game step, the render pipeline and
 * the audio ring.
 *
 * Render pipeline (render_frame):
 *   1. three.c draws the scene into our framebuffer (RGBA8 + depth), tone
 *      mapped and sRGB encoded, as the page's WebGLRenderer draws its canvas
 *   2. the depth of the opaque scene again (t3_renderer_render_depth), so the
 *      effects test against it
 *   3. fx.c: the page's ShaderMaterial effects (sparks, trails, rings, bursts,
 *      dust, light cones), blended on encoded values as WebGL blends them
 *   4. overlays: vignette, grain, HUD, screens, flash, dance (overlay.c)
 *   5. blit to the cart's framebuffer */
#include "wasmcart.h"
#include "wc_gl.h"
#include "three.h"
#include "ck.h"
#include "cv.h"
#include "overlay.h"
#include "game.h"
#include "input.h"
#include "audio.h"
#include "present.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AUDIO_CAP 8192

static uint32_t framebuffer[4];
static float audio_ring[AUDIO_CAP * 2];
static uint32_t audio_cursor;
static wc_pad_t pads[4];
static uint8_t keys[32];
static wc_pointer_t pointers[10];
static wc_time_t tm;
static wc_info_t info;
static wc_host_info_t host_info;
static struct { uint32_t magic; char name[16]; char weapon[12]; uint8_t pad[32]; } SAVE;   /* the page's localStorage */

t3_renderer *CK_R;
double ck_clock_s;
static uint32_t seed = 7;
static GLuint fbo, fbo_color, fbo_depth;
/* the frame the host shows (present.h): CK_W x CK_H, or scaled up to the host's bigger 16:9 size */
static int out_w = CK_W, out_h = CK_H;
static int boot = 0;
static uint32_t audio_rate = 48000;

__attribute__((export_name("wc_get_info")))
wc_info_t *wc_get_info(void) {
  info.version = WC_ABI_VERSION;
  info.width = out_w; info.height = out_h;
  info.fb_ptr = (uint32_t)(uintptr_t)framebuffer;
  info.audio_ptr = (uint32_t)(uintptr_t)audio_ring;
  info.audio_cap = AUDIO_CAP;
  info.audio_write_ptr = (uint32_t)(uintptr_t)&audio_cursor;
  info.input_ptr = (uint32_t)(uintptr_t)pads;
  info.time_ptr = (uint32_t)(uintptr_t)&tm;
  info.host_info_ptr = (uint32_t)(uintptr_t)&host_info;
  info.keys_ptr = (uint32_t)(uintptr_t)keys;
  info.pointer_ptr = (uint32_t)(uintptr_t)pointers;
  info.flags = WC_FLAG_AUDIO_F32 | WC_FLAG_KEYBOARD | WC_FLAG_POINTER | WC_FLAG_NET_PEER;
  info.gpu_api = 1;
  info.audio_sample_rate = 0;
  info.save_ptr = (uint32_t)(uintptr_t)&SAVE;
  info.save_size = sizeof SAVE;
  return &info;
}

__attribute__((export_name("wc_set_seed")))
void wc_set_seed(uint32_t s) { seed = s ? s : 7; }

const char *ck_save_get(const char *key, const char *def) {
  if (SAVE.magic != 0x434b3031) return def;
  if (!strcmp(key, "ck_name")) return SAVE.name[0] ? SAVE.name : def;
  if (!strcmp(key, "ck_weapon")) return SAVE.weapon[0] ? SAVE.weapon : def;
  return def;
}
void ck_save_set(const char *key, const char *val) {
  if (SAVE.magic != 0x434b3031) { memset(&SAVE, 0, sizeof SAVE); SAVE.magic = 0x434b3031; }
  if (!strcmp(key, "ck_name")) { strncpy(SAVE.name, val, sizeof SAVE.name - 1); }
  if (!strcmp(key, "ck_weapon")) { strncpy(SAVE.weapon, val, sizeof SAVE.weapon - 1); }
}

static void fonts_init(void) {
  static const char *f[3] = {"Archivo.ttf", "IBMPlexMono.ttf", "JetBrainsMono.ttf"};
  for (int i = 0; i < 3; i++) {
    size_t n; uint8_t *b = ck_asset(f[i], &n);
    if (b) cv_font_add(i, b, (int)n);   /* kept for the life of the cart */
  }
}

__attribute__((export_name("wc_init")))
void wc_init(void) {
  extern const struct t3_gen_table ck_gl;
  t3_register_program_table(&ck_gl, NULL);
  ck_seed(seed);
  audio_rate = host_info.audio_sample_rate ? host_info.audio_sample_rate : 48000;

  CK_R = t3_renderer_new(CK_W, CK_H);
  /* new ir({ antialias: true }): toneMapping ACESFilmic, exposure 1.05, PCF shadows */
#ifndef CK_PROF_NOMSAA
  t3_renderer_set_antialias(CK_R, 4);
#endif
  t3_renderer_set_tone_mapping(CK_R, T3_ACES_FILMIC_TONE_MAPPING, 1.05f);
#ifndef CK_PROF_NOSHADOW
  t3_renderer_set_shadow_map(CK_R, true, T3_PCF_SHADOW_MAP);
#endif

  glGenTextures(1, &fbo_color);
  glBindTexture(GL_TEXTURE_2D, fbo_color);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, CK_W, CK_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenRenderbuffers(1, &fbo_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, fbo_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, CK_W, CK_H);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_color, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo_depth);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  t3_renderer_set_framebuffer(CK_R, fbo);
  present_size(&host_info, CK_W, CK_H, &out_w, &out_h);
  if (out_w != CK_W || out_h != CK_H) {
    /* present_scale samples the target between texels */
    glBindTexture(GL_TEXTURE_2D, fbo_color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  info.width = out_w; info.height = out_h;

  fonts_init();
  ov_init();
  input_init();
  audio_init(audio_rate);
  game_init();
}

/* the frame's 3D: scene, then its depth, then the effects; overlays and the blit follow */
void render_scene(t3_scene *scene, t3_camera *cam) {
  t3_renderer_set_framebuffer(CK_R, fbo);
  t3_renderer_render(CK_R, scene, cam);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, CK_W, CK_H);
  glDisable(GL_SCISSOR_TEST);
  glDepthMask(GL_TRUE);
  glClear(GL_DEPTH_BUFFER_BIT);
  /* the caster programs write colour too: depth only here */
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
#ifndef CK_PROF_NODEPTH
  t3_renderer_render_depth(CK_R, scene, cam);
#endif
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  t3_renderer_reset_bindings(CK_R);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, CK_W, CK_H);
}
void render_clear(uint32_t rgb) {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, CK_W, CK_H);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, 1);
  glDepthMask(GL_TRUE);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
static void present(void) {
  if (out_w != CK_W || out_h != CK_H) {
    present_scale(fbo_color, CK_W, CK_H, out_w, out_h);
    t3_renderer_reset_bindings(CK_R);
    return;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBlitFramebuffer(0, 0, CK_W, CK_H, 0, 0, CK_W, CK_H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  t3_renderer_reset_bindings(CK_R);
}

__attribute__((export_name("wc_render")))
void wc_render(void) {
  /* requestAnimationFrame: dt = min(0.05, max(0, delta)) */
  float dt = tm.delta_ms > 0 ? (float)(tm.delta_ms / 1000.0) : 1.f / 60.f;
  if (dt > 0.05f) dt = 0.05f;
  ck_clock_s += tm.delta_ms > 0 ? tm.delta_ms / 1000.0 : 1.0 / 60.0;

  /* the host reports the pointer in the shown frame's pixels; the game works in CK_W x CK_H */
  wc_pointer_t ptrs[10];
  memcpy(ptrs, pointers, sizeof ptrs);
  if (out_w != CK_W || out_h != CK_H)
    for (int i = 0; i < 10; i++) { ptrs[i].x = (int16_t)(ptrs[i].x * CK_W / out_w); ptrs[i].y = (int16_t)(ptrs[i].y * CK_H / out_h); }
  input_frame(pads, keys, ptrs);
#ifdef CK_PROF
  struct timespec t0, t1, t2, t3; clock_gettime(CLOCK_MONOTONIC, &t0);
#endif
  if (boot >= 0) {
    if (game_boot(boot)) boot = -1; else boot++;
  } else {
    game_frame(dt);
  }
#ifdef CK_PROF
  clock_gettime(CLOCK_MONOTONIC, &t1);
#endif
  game_draw();            /* render_scene / render_clear, fx, overlays */
#ifdef CK_PROF
  clock_gettime(CLOCK_MONOTONIC, &t2);
#endif
  present();
#ifdef CK_PROF
  clock_gettime(CLOCK_MONOTONIC, &t3);
  {
    static double a, b, c; static int n;
    a += (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    b += (t2.tv_sec - t1.tv_sec) * 1e3 + (t2.tv_nsec - t1.tv_nsec) / 1e6;
    c += (t3.tv_sec - t2.tv_sec) * 1e3 + (t3.tv_nsec - t2.tv_nsec) / 1e6;
    if (++n == 120) { const t3_render_info *ri = t3_renderer_info(CK_R); ck_log("[prof] frame %.2f draw %.2f present %.2f ms, calls %u tris %u", a / n, b / n, c / n, ri->calls, ri->triangles); a = b = c = 0; n = 0; }
  }
#endif
  input_end_frame();

  audio_render(audio_ring, AUDIO_CAP, &audio_cursor, audio_rate, tm.delta_ms > 0 ? tm.delta_ms : dt * 1000.0);

  static int logged;
  const char *miss = t3_renderer_generated_missing(CK_R);
  if (miss && *miss && logged < 4 && tm.frame % 120 == 0) { wc_log(miss, (unsigned)strlen(miss)); logged++; }
}

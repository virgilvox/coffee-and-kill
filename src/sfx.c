/* sfx.c: the page's sound effects (SA) and its output chain (ie()).
 *
 * Every SA voice is a small WebAudio graph built per call (oscillators with
 * exponential ramps, filtered noise from a random 1.5 s buffer at a random
 * offset). tools/audio_sfx.mjs renders each one through Chrome's own
 * WebAudio (an OfflineAudioContext running the page's SA code) into
 * assets/music/sfx.flac (16 bit, at half level): several variants of each voice that draws noise, so
 * the randomness stays. What remains live is the graph after the voices:
 *
 *   voices -> y8 (gain 1) --\
 *                            pD (0.8; 0 when muted) -> DynamicsCompressor(-14 dB, 5:1) -> out
 *   music  -> HD (0.36) ----/   (QP.duck automates HD)
 *
 * The compressor is Chromium's DynamicsCompressorKernel (knee 30, attack
 * .003, release .25, the default makeup gain), ported below. The chain runs at
 * 48 kHz (the page's context rate) and is resampled to the host's rate. */
#include "audio.h"
#include "kh/kaudio.h"
#include "kh/ksound.h"
#include "ck.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include "dr_libs/dr_flac.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void music_init(void);
void music_render(float *L, float *R, int n, double t);
void music_set_muted(bool m);

#define RATE 48000.0
#define BLOCK 128

/* ---- the baked voices ---- */
typedef struct { char name[12]; float arg; uint32_t start, frames; } variant_t;
static variant_t *var; static int nvar;
static short *pcm; static uint32_t npcm;
typedef struct { const short *p; uint32_t n, pos; } voice_t;
#define MAXV 48
#define VSAY (MAXV - 1)   /* the last slot is kept for a fighter's spoken line, one at a time */
static voice_t voices[MAXV];

/* the fighters' spoken lines: a second bank in the same shape as the SFX one (lines_index.bin +
 * lines.flac), played on the reserved VSAY slot so a line is never cut by a sound effect */
static variant_t *lvar; static int nlvar; static short *lpcm; static uint32_t nlpcm;

void (*sfx_tap)(const char *name, float arg);

/* a CKSI index plus its mono FLAC into a bank (variants and the s16 pcm, half level) */
static void load_bank(const char *index, const char *flac, variant_t **vp, int *np, short **pp, uint32_t *cp) {
  size_t n; uint8_t *idx = ck_asset(index, &n);
  if (!idx || n < 8 || memcmp(idx, "CKSI", 4)) { free(idx); return; }
  uint32_t c; memcpy(&c, idx + 4, 4);
  variant_t *v = calloc(c, sizeof *v);
  const uint8_t *q = idx + 8;
  for (uint32_t i = 0; i < c; i++) {
    int l = *q++;
    memcpy(v[i].name, q, l < 11 ? l : 11); q += l;
    memcpy(&v[i].arg, q, 4); memcpy(&v[i].start, q + 4, 4); memcpy(&v[i].frames, q + 8, 4); q += 12;
  }
  *vp = v; *np = (int)c; free(idx);
  size_t m; uint8_t *b = ck_asset(flac, &m);
  if (!b) return;
  unsigned ch, rate; drflac_uint64 total = 0;
  *pp = drflac_open_memory_and_read_pcm_frames_s16(b, m, &ch, &rate, &total, NULL);
  *cp = *pp && ch == 1 ? (uint32_t)total : 0;
  free(b);
}

static void load_sfx(void) {
  load_bank("sfx_index.bin", "sfx.flac", &var, &nvar, &pcm, &npcm);
  load_bank("lines_index.bin", "lines.flac", &lvar, &nlvar, &lpcm, &nlpcm);
}

/* a fighter's line: pick a variant of name at random, play it on the reserved slot */
void voice_say(const char *name) {
  if (!lpcm || !name) return;
  int best[8], nb = 0;
  for (int i = 0; i < nlvar; i++) if (!strcmp(lvar[i].name, name) && nb < 8) best[nb++] = i;
  if (!nb) return;
  const variant_t *v = &lvar[best[nb > 1 ? (int)(ck_rand() * nb) % nb : 0]];
  if (v->start + v->frames > nlpcm) return;
  voices[VSAY].p = lpcm + v->start; voices[VSAY].n = v->frames; voices[VSAY].pos = 0;
}

/* one variant of name (with the arg nearest to arg), picked at random among its renders */
static void play(const char *name, float arg) {
  int best[16], nb = 0; float bd = 1e9f;
  for (int i = 0; i < nvar; i++) {
    if (strcmp(var[i].name, name)) continue;
    float dd = fabsf(var[i].arg - arg);
    if (dd < bd - 1e-4f) { bd = dd; nb = 0; }
    if (fabsf(dd - bd) <= 1e-4f && nb < 16) best[nb++] = i;
  }
  if (!nb || !pcm) return;
  const variant_t *v = &var[best[nb > 1 ? (int)(ck_rand() * nb) % nb : 0]];
  if (v->start + v->frames > npcm) return;
  int slot = -1; uint32_t most = 0;
  for (int i = 0; i < VSAY; i++) { if (!voices[i].p) { slot = i; break; } if (voices[i].pos >= most) { most = voices[i].pos; slot = i; } }
  voices[slot].p = pcm + v->start; voices[slot].n = v->frames; voices[slot].pos = 0;
}

/* ---- AudioParam (HD.gain): setValueAtTime, linearRampToValueAtTime, cancelScheduledValues ---- */
typedef struct { int type; double t; double v; } pev;   /* 0 set, 1 linear ramp */
static struct { double def; pev ev[16]; int n; } hd = {0.36};
static double param_at(double t) {
  double v = hd.def, vt = 0;
  for (int i = 0; i < hd.n; i++) {
    const pev *e = &hd.ev[i];
    if (e->t <= t) { v = e->v; vt = e->t; continue; }
    if (e->type == 1) return v + (e->v - v) * (t - vt) / (e->t - vt);   /* in the ramp to e */
    return v;                                                          /* waiting for a set */
  }
  return v;
}
static void param_cancel(double t) { int k = 0; for (int i = 0; i < hd.n; i++) if (hd.ev[i].t < t) hd.ev[k++] = hd.ev[i]; hd.n = k; }
static void param_push(int type, double t, double v) {
  if (hd.n == 16) { memmove(hd.ev, hd.ev + 1, sizeof(pev) * 15); hd.n--; }
  int i = hd.n;
  while (i > 0 && hd.ev[i - 1].t > t) { hd.ev[i] = hd.ev[i - 1]; i--; }
  hd.ev[i].type = type; hd.ev[i].t = t; hd.ev[i].v = v; hd.n++;
}

/* ---- Chromium's DynamicsCompressorKernel ---- */
#define PRE_MAX 1024
#define PRE_MASK (PRE_MAX - 1)
static struct {
  float thr_db, knee_db, ratio, slope, lin_thr, knee_thr, knee_thr_db, y_knee_thr_db, k;
  float detector_average, compressor_gain, max_attack_db;
  float pre[2][PRE_MAX]; int rd, wr;
  float kA, kB, kC, kD, kE, attack_frames, sat_release_frames, master_gain;
} cp;
static float lin2db(float x) { return 20.f * log10f(x); }
static float db2lin(float x) { return powf(10.f, 0.05f * x); }
static float knee_curve(float x, float k) {
  if (x < cp.lin_thr) return x;
  return cp.lin_thr + (1 - expf(-k * (x - cp.lin_thr))) / k;
}
static float saturate(float x, float k) {
  if (x < cp.knee_thr) return knee_curve(x, k);
  float xdb = lin2db(x), ydb = cp.y_knee_thr_db + cp.slope * (xdb - cp.knee_thr_db);
  return db2lin(ydb);
}
static float slope_at(float x, float k) {
  if (x < cp.lin_thr) return 1;
  float x2 = x * 1.001f;
  float xdb = lin2db(x), x2db = lin2db(x2), ydb = lin2db(knee_curve(x, k)), y2db = lin2db(knee_curve(x2, k));
  return (y2db - ydb) / (x2db - xdb);
}
static float k_at_slope(float desired) {
  float xdb = cp.thr_db + cp.knee_db, x = db2lin(xdb);
  float mink = 0.1f, maxk = 10000, k = 5;
  for (int i = 0; i < 15; i++) {
    float s = slope_at(x, k);
    if (s < desired) maxk = k; else mink = k;
    k = sqrtf(mink * maxk);
  }
  return k;
}
static void comp_init(float thr, float knee, float ratio, float attack, float release) {
  memset(&cp, 0, sizeof cp);
  cp.thr_db = thr; cp.lin_thr = db2lin(thr); cp.knee_db = knee; cp.ratio = ratio; cp.slope = 1 / ratio;
  float k = k_at_slope(1 / ratio);
  cp.knee_thr_db = thr + knee; cp.knee_thr = db2lin(cp.knee_thr_db);
  cp.y_knee_thr_db = lin2db(knee_curve(cp.knee_thr, k));
  cp.k = k;
  float full_range_gain = saturate(1, k);
  float makeup = powf(1 / full_range_gain, 0.6f);
  cp.master_gain = makeup;   /* post gain 0 dB */
  cp.attack_frames = fmaxf(0.001f, attack) * (float)RATE;
  float rf = (float)RATE * release;
  cp.sat_release_frames = 0.0025f * (float)RATE;
  float y1 = rf * 0.09f, y2 = rf * 0.16f, y3 = rf * 0.42f, y4 = rf * 0.98f;
  cp.kA = 0.9999999999999998f * y1 + 1.8432219684323923e-16f * y2 - 1.9373394351676423e-16f * y3 + 8.824516011816245e-18f * y4;
  cp.kB = -1.5788320352845888f * y1 + 2.3305837032074286f * y2 - 0.9141194204840429f * y3 + 0.1623677525612032f * y4;
  cp.kC = 0.5334142869106424f * y1 - 1.272736789213631f * y2 + 0.9258856042207512f * y3 - 0.18656310191776226f * y4;
  cp.kD = 0.08783463138207234f * y1 - 0.1694162967925622f * y2 + 0.08588057951595272f * y3 - 0.00429891410546283f * y4;
  cp.kE = -0.042416883008123074f * y1 + 0.1115693827987602f * y2 - 0.09764676325265872f * y3 + 0.028494263462021576f * y4;
  cp.detector_average = 0; cp.compressor_gain = 1; cp.max_attack_db = -1;
  cp.rd = 0; cp.wr = (int)(0.006f * (float)RATE);   /* pre-delay 6 ms */
}
static void comp_process(float *L, float *R, int frames) {
  const float pi2 = 1.57079632679489661923f;
  int idx = 0;
  for (int div = 0; div < frames / 32; div++) {
    if (isnan(cp.detector_average) || isinf(cp.detector_average)) cp.detector_average = 1;
    float desired = cp.detector_average;
    float scaled_desired = asinf(desired) / pi2;
    float envelope_rate;
    bool releasing = scaled_desired > cp.compressor_gain;
    float diff_db = lin2db(cp.compressor_gain / scaled_desired);
    if (releasing) {
      cp.max_attack_db = -1;
      if (isnan(diff_db) || isinf(diff_db)) diff_db = -1;
      float x = diff_db; x = x < -12 ? -12 : x > 0 ? 0 : x; x = 0.25f * (x + 12);
      float x2 = x * x, x3 = x2 * x, x4 = x2 * x2;
      float release_frames = cp.kA + cp.kB * x + cp.kC * x2 + cp.kD * x3 + cp.kE * x4;
      envelope_rate = db2lin(5.f / release_frames);
    } else {
      if (isnan(diff_db) || isinf(diff_db)) diff_db = 1;
      if (cp.max_attack_db == -1 || cp.max_attack_db < diff_db) cp.max_attack_db = diff_db;
      float eff = fmaxf(0.5f, cp.max_attack_db);
      envelope_rate = 1 - powf(0.25f / eff, 1 / cp.attack_frames);
    }
    for (int f = 0; f < 32; f++, idx++) {
      float in = 0;
      float s0 = L[idx], s1 = R[idx];
      cp.pre[0][cp.wr] = s0; cp.pre[1][cp.wr] = s1;
      float a0 = fabsf(s0), a1 = fabsf(s1);
      in = a0 > a1 ? a0 : a1;
      float shaped = saturate(in, cp.k);
      float att = in <= 0.0001f ? 1 : shaped / in;
      float att_db = -lin2db(att); if (att_db < 2) att_db = 2;
      float sat_rate = db2lin(att_db / cp.sat_release_frames) - 1;
      float rate = att > cp.detector_average ? sat_rate : 1;
      cp.detector_average += (att - cp.detector_average) * rate;
      if (cp.detector_average > 1) cp.detector_average = 1;
      if (isnan(cp.detector_average) || isinf(cp.detector_average)) cp.detector_average = 1;
      if (envelope_rate < 1) cp.compressor_gain += (scaled_desired - cp.compressor_gain) * envelope_rate;
      else { cp.compressor_gain *= envelope_rate; if (cp.compressor_gain > 1) cp.compressor_gain = 1; }
      float post = sinf(pi2 * cp.compressor_gain);
      float total = cp.master_gain * post;
      L[idx] = cp.pre[0][cp.rd] * total; R[idx] = cp.pre[1][cp.rd] * total;
      cp.rd = (cp.rd + 1) & PRE_MASK; cp.wr = (cp.wr + 1) & PRE_MASK;
    }
  }
}

/* ---- the chain ---- */
static bool muted;
static double t_audio;                 /* AudioContext.currentTime: the next 48 kHz frame */
static float blk_l[BLOCK], blk_r[BLOCK];
static int blk_pos = BLOCK;            /* the next unread frame of the current 48 kHz block */
static double rs_frac;                 /* the resampler's position between f0 and f1 */
static float h[4][2];                  /* the last four 48 kHz frames, for the cubic */
static double carry;
static bool inited;

static void render_block(void) {
  memset(blk_l, 0, sizeof blk_l); memset(blk_r, 0, sizeof blk_r);
  music_render(blk_l, blk_r, BLOCK, t_audio);
  /* while KEYHOLDER's fight music plays (a KEYHOLDER stage) the page's band steps aside, eased */
  { static float band = 1; float to = kmusic_playing() ? 0.f : 1.f;
    for (int i = 0; i < BLOCK; i++) { band += (to - band) * 0.0005f; blk_l[i] *= band; blk_r[i] *= band; } }
  float pd = muted ? 0.f : 0.8f;
  for (int i = 0; i < BLOCK; i++) {
    double t = t_audio + i / RATE;
    float g = (float)param_at(t);
    float s = 0;
    for (int v = 0; v < MAXV; v++) {
      voice_t *vo = &voices[v];
      if (!vo->p) continue;
      if (i == 0 && vo->pos >= vo->n) { vo->p = NULL; continue; }
      if (vo->pos < vo->n) s += vo->p[vo->pos++] * (2.f / 32768.f);   /* stored at half level */
    }
    blk_l[i] = (blk_l[i] * g + s) * pd;
    blk_r[i] = (blk_r[i] * g + s) * pd;
  }
  for (int v = 0; v < MAXV; v++) if (voices[v].p && voices[v].pos >= voices[v].n) voices[v].p = NULL;
  kaudio_mix(blk_l, blk_r, BLOCK, pd * 0.55f);
  comp_process(blk_l, blk_r, BLOCK);
  t_audio += BLOCK / RATE;
  /* forget automation that is over */
  while (hd.n > 1 && hd.ev[1].t <= t_audio) { memmove(hd.ev, hd.ev + 1, sizeof(pev) * (hd.n - 1)); hd.n--; }
  blk_pos = 0;
}
static void next_frame(float *l, float *r) {
  if (blk_pos >= BLOCK) render_block();
  *l = blk_l[blk_pos]; *r = blk_r[blk_pos]; blk_pos++;
}

void audio_init(uint32_t rate) {
  (void)rate;
  if (inited) return;
  inited = true;
  load_sfx();
  comp_init(-14, 30, 5, 0.003f, 0.25f);
  music_init();
  kaudio_init(48000);   /* KEYHOLDER's synth, mixed into the same block (kh/kaudio.c) */
}

/* the output ring at the host's rate, as much as the host's time asks */
void audio_render(float *ring, uint32_t cap, uint32_t *cursor, uint32_t rate, double ms) {
  if (!inited) return;
  if (!rate) rate = 48000;
  double want = (double)rate * ms / 1000.0 + carry;
  int frames = (int)want;
  carry = want - frames;
  if (frames <= 0) return;
  if ((uint32_t)frames > cap / 2) { frames = (int)(cap / 2); carry = 0; }
  uint32_t c = *cursor;
  if (rate == 48000) {
    for (int i = 0; i < frames; i++) { next_frame(&ring[c * 2], &ring[c * 2 + 1]); c = (c + 1) % cap; }
  } else {
    double step = RATE / rate;
    for (int i = 0; i < frames; i++) {
      while (rs_frac >= 1.0) { memmove(h[0], h[1], sizeof(float) * 6); next_frame(&h[3][0], &h[3][1]); rs_frac -= 1.0; }
      float x = (float)rs_frac;
      for (int ch = 0; ch < 2; ch++) {   /* Catmull-Rom between h[1] and h[2] */
        float p0 = h[0][ch], p1 = h[1][ch], p2 = h[2][ch], p3 = h[3][ch];
        ring[c * 2 + ch] = p1 + 0.5f * x * (p2 - p0 + x * (2 * p0 - 5 * p1 + 4 * p2 - p3 + x * (3 * (p1 - p2) + p3 - p0)));
      }
      rs_frac += step;
      c = (c + 1) % cap;
    }
  }
  *cursor = c;
}

double audio_time(void) { return t_audio - (BLOCK - blk_pos) / RATE; }
double audio_latency(void) { return 0.05; }

/* ---- SA ---- */
static void tap(const char *n, float a) { if (sfx_tap) sfx_tap(n, a); ksound_echo(n, a); }   /* KEYHOLDER layers its own (kh/ksound.c) */
void sfx_duck(float s, float a) {
  double P = audio_time(), v = param_at(P);
  param_cancel(P);
  param_push(0, P, v);
  param_push(1, P + 0.015, 0.36 * s);
  param_push(1, P + a, 0.36);
}
void sfx_whoosh(float s) { tap("whoosh", s); play("whoosh", s); }
void sfx_hit(bool heavy) { tap("hit", heavy ? 1.f : 0.f); sfx_duck(heavy ? 0.45f : 0.65f, heavy ? 0.45f : 0.28f); play(heavy ? "hit1" : "hit0", heavy ? 1.f : 0.f); }
void sfx_clang(void) { tap("clang", 0); play("clang", 0); }
void sfx_block(void) { tap("block", 0); tap("clang", 0); play("block", 0); }        /* QP.block calls QP.clang */
void sfx_kick(void) { tap("kick", 0); play("kick", 0); }
void sfx_shock(void) { tap("shock", 0); sfx_duck(0.35f, 0.9f); play("shock", 0); }
void sfx_ko(void) { tap("ko", 0); sfx_duck(0.3f, 1.4f); tap("clang", 0); play("ko", 0); }   /* QP.ko calls QP.clang */
void sfx_bell(void) { tap("bell", 0); play("bell", 0); }
void sfx_ui(void) { play("ui", 0); }
void sfx_ui_ok(void) { play("uiOk", 0); }
void sfx_step(void) { play("step", 0); }
void sfx_dodge(void) { tap("dodge", 0); play("dodge", 0); }
void sfx_special(void) { tap("special", 0); play("special", 0); }
void sfx_fight(void) { tap("fight", 0); play("fight", 0); }
void sfx_win(void) { tap("win", 0); play("win", 0); }

bool sfx_toggle_mute(void) { muted = !muted; music_set_muted(muted); return muted; }
bool sfx_is_muted(void) { return muted; }

/* the guest replays the host's SFX by name (SA[name](...args)) */
void sfx_by_name(const char *n, float a) {
  if (!strcmp(n, "whoosh")) sfx_whoosh(a);
  else if (!strcmp(n, "hit")) sfx_hit(a != 0);
  else if (!strcmp(n, "clang")) sfx_clang();
  else if (!strcmp(n, "block")) sfx_block();
  else if (!strcmp(n, "kick")) sfx_kick();
  else if (!strcmp(n, "shock")) sfx_shock();
  else if (!strcmp(n, "ko")) sfx_ko();
  else if (!strcmp(n, "bell")) sfx_bell();
  else if (!strcmp(n, "dodge")) sfx_dodge();
  else if (!strcmp(n, "special")) sfx_special();
  else if (!strcmp(n, "fight")) sfx_fight();
  else if (!strcmp(n, "win")) sfx_win();
}

#ifdef SFX_TEST
/* tools/audio_comptest.mjs drives the compressor alone, natively */
void sfx_test_comp(float *L, float *R, int frames) {
  comp_init(-14, 30, 5, 0.003f, 0.25f);
  for (int i = 0; i + BLOCK <= frames; i += BLOCK) comp_process(L + i, R + i, BLOCK);
}
#endif

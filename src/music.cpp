// music.cpp: the page's Bellows band (tt in the page), played from baked
// stems. tools/audio_bake.mjs runs the page's own Bellows kernel (its
// AudioWorklet code, in Node) under the page's own score (Dl, wl, gl, il,
// j0, rl) and Transport, and writes what the worklet outputs at masterGain 1:
// one steady loop per intensity band (il 8 bars, gl 8 bars, wl 16 bars at
// 140 bpm, swing .03) and each dance song from its first step. The kernel
// message stream of that harness matches the live page's note for note
// (tools/audio_compare.mjs), so the stems are the page's music.
//
// What stays live is everything that depends on play: the Transport's beat
// clock (the score indexes its bars by the clock's sixteenth, so a band
// always resumes where the clock is), a level change (tt.setLevel: the band
// changes, panic releases what was sounding and the scheduler's lookahead is
// lost), masterGain (SB per mode times E0 of the level, mute and N, through
// the kernel's 20 ms smoother), and the dance (tt.dance: panic, the song from
// the next sixteenth of its tempo, the Promise's start time).
//
// Stems are FLAC (16 bit, lossless; third_party/dr_libs/dr_flac.h).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dr_libs/dr_flac.h"

extern "C" {
#include "ck.h"
#include "audio.h"
#include "songs.h"
}
#include <stdio.h>

namespace {

constexpr double kRate = 48000.0;                 // the page's AudioContext rate (and the stems')
constexpr double kSps = 60.0 / 140.0 / 4.0;       // a sixteenth at 140
constexpr double kLookahead = 0.11;               // scheduler horizon .12, ticked every 25 ms
constexpr double kStart = 0.15;                   // boot, Dl, vP.start(): transport.start(now + .05)

// a stem: the fight loops are decoded whole at boot (10 MB of int16); a dance
// song streams, read forward from its start as it plays
struct Stem {
  uint8_t *bytes = nullptr; size_t len = 0;
  uint32_t frames = 0;
  short *all = nullptr;                   // whole (fight)
  drflac *dec = nullptr;                  // streaming (dance)
  static constexpr uint32_t kChunk = 4096;
  short chunk[kChunk * 2];
  uint32_t c0 = 0, cn = 0;                // the chunk holds frames [c0, c0 + cn)
  bool load(const char *name, bool whole) {
    bytes = ck_asset(name, &len);
    if (!bytes) return false;
    if (whole) {
      unsigned ch, rate; drflac_uint64 n = 0;
      all = drflac_open_memory_and_read_pcm_frames_s16(bytes, len, &ch, &rate, &n, nullptr);
      frames = all ? (uint32_t)n : 0;
      free(bytes); bytes = nullptr; len = 0;
      if (!all || ch != 2) { ck_log("music: bad stem %s", name); frames = 0; return false; }
      return true;
    }
    dec = drflac_open_memory(bytes, len, nullptr);
    if (!dec) { ck_log("music: bad stem %s", name); return false; }
    frames = (uint32_t)dec->totalPCMFrameCount;
    return true;
  }
  void rewind() { if (dec) { drflac_seek_to_pcm_frame(dec, 0); c0 = 0; cn = 0; } }
  inline void at(uint32_t p, float &l, float &r) {
    if (p >= frames) { l = r = 0; return; }
    if (all) { l = all[p * 2] / 32768.f; r = all[p * 2 + 1] / 32768.f; return; }
    if (!dec) { l = r = 0; return; }
    if (p < c0) { drflac_seek_to_pcm_frame(dec, p); c0 = p; cn = 0; }
    while (p >= c0 + cn) {
      c0 += cn;
      if (p >= c0 + kChunk) { drflac_seek_to_pcm_frame(dec, p); c0 = p; }
      cn = (uint32_t)drflac_read_pcm_frames_s16(dec, kChunk, chunk);
      if (!cn) { l = r = 0; return; }
    }
    uint32_t i = p - c0;
    l = chunk[i * 2] / 32768.f; r = chunk[i * 2 + 1] / 32768.f;
  }
};

Stem fight[3];
Stem dance[5];
const double kLoopSteps[3] = {128, 128, 256};

// the Transport: beat 0 at origin_t, bpm since; setBpm re-origins at the beat of now
struct Clock { double origin_t = kStart, origin_beat = 0, bpm = 140; } clk;
double beat_at(double t) { return clk.origin_beat + (t - clk.origin_t) * clk.bpm / 60.0; }
void set_bpm(double bpm, double now) { clk.origin_beat = beat_at(now); clk.origin_t = now; clk.bpm = bpm; }

// a band playing: its stem, where it began (or begins), its envelope
struct Play {
  int band = -1;            // 0 il, 1 gl, 2 wl
  double pos = 0;           // stem frame, advancing 1 per output frame at 48 kHz
  double start_t = 0;       // the audio time it sounds from (the first notes after the lookahead)
  double gain = 0;          // 0..1: fading in after a start, decaying after a panic
  bool releasing = false;
};
Play cur, prev;

struct Dance { int song = -1; double t0 = 0; double gain = 1; bool releasing = false; } dn;

// tt state
double Sw = 0;                  // level
bool h0 = false, Or = true;     // muted, music on
double SB = 1.6;                // per mode trim (_7 in a dance)
bool U8 = false;                // dancing
double mg_v = 0, mg_target = 0, mg_step = 0;   // the kernel's masterGain smoother (ot, 20 ms)
const double mg_rate = 1.0 / (kRate * 0.02);
double now_t = 0;               // the audio time of the next frame rendered
bool loaded = false;

int band_of(double s) { return s > 0.5 ? 2 : s > 0.2 ? 1 : 0; }
double e0(double s) { return (h0 || !Or) ? 0 : s >= 0.9 ? 1 : s >= 0.3 ? 0.95 : 0.6; }   // E0, $7
void ld() {   // LD(): masterGain(SB * E0)
  mg_target = SB * e0(Sw);
  mg_step = (mg_target - mg_v) * mg_rate;
}
double fight_pos(int band, double t) {
  double steps = beat_at(t) * 4.0, n = kLoopSteps[band];
  double s = fmod(steps, n); if (s < 0) s += n;
  return s * kSps * kRate;
}
void start_band(int band, double t) {   // the score resumes: the first notes after the lookahead
  cur.band = band; cur.start_t = t; cur.pos = fight_pos(band, t); cur.gain = 0; cur.releasing = false;
}
void panic_band() {
  if (cur.band >= 0) { prev = cur; prev.releasing = true; }
  cur.band = -1;
}

}  // namespace

extern "C" void music_init(void) {
  static const char *names[3] = {"fight0.flac", "fight1.flac", "fight2.flac"};
  for (int i = 0; i < 3; i++) fight[i].load(names[i], true);
  for (int i = 0; i < 5; i++) {
    const song_t *s = song_get(i);
    char n[40]; snprintf(n, sizeof n, "dance_%s.flac", s->id);
    dance[i].load(n, false);
  }
  // Gr.boot({ masterGain: 0 }), Dl(), vP.start(), LD()
  mg_v = 0;
  start_band(band_of(Sw), kStart);
  ld();
  loaded = true;
}

extern "C" void music_set_level(float s) {
  int b0 = band_of(Sw), b1 = band_of(s);
  Sw = s;
  if (b0 != b1 && loaded && !U8) {   // vP.panic(): what sounds is released, the queued lookahead is lost
    panic_band();
    start_band(b1, now_t + kLookahead);
  } else if (!U8 && cur.band < 0) start_band(b1, now_t + kLookahead);
  ld();
}

extern "C" bool music_toggle(void) { Or = !Or; ld(); return Or; }
extern "C" void music_set_muted(bool m) { h0 = m; ld(); }

extern "C" double music_dance_start(int song) {
  if (!loaded || song < 0 || song >= 5 || !dance[song].dec) return -1;
  const song_t *s = song_get(song);
  static const double trim[5] = {1.6, 1.3, 1.4, 1.5, 1.5};   // _7: funk disco rave surf lounge
  panic_band();
  if (dn.song >= 0) dn.releasing = true;
  SB = trim[song];
  ld();
  U8 = true;
  double now = now_t;
  set_bpm(s->bpm, now);                // vP.bpm(A.bpm); swing 0
  // rl() starts the song on the clock's next sixteenth past what the scheduler already queued
  double b = beat_at(now + kLookahead);
  double step = ceil(b * 4.0 - 1e-9);
  double t0 = clk.origin_t + (step / 4.0 - clk.origin_beat) * 60.0 / clk.bpm;
  dance[song].rewind();
  dn.song = song; dn.t0 = t0; dn.gain = 1; dn.releasing = false;
  return t0;
}

extern "C" void music_dance_stop(void) {
  if (!loaded) return;
  if (dn.song >= 0) dn.releasing = true;   // panic
  SB = 1.6;
  ld();
  U8 = false;
  set_bpm(140, now_t);                     // bpm 140, swing .03
  start_band(band_of(Sw), now_t + kLookahead);
}

// the worklet's output for n frames at 48 kHz from audio time t, added into L and R
extern "C" void music_render(float *L, float *R, int n, double t) {
  if (!loaded) return;
  const double rel = exp(-1.0 / (0.3 * kRate));        // the released voices and the buses' tails
  const double fin = 1.0 / (0.03 * kRate);             // the first notes after a start
  for (int i = 0; i < n; i++) {
    double tt = t + i / kRate;
    double m = mg_v;
    if (mg_step != 0) {                                  // ot.next()
      double v = mg_v + mg_step;
      if ((mg_step > 0 && v >= mg_target) || (mg_step < 0 && v <= mg_target)) { mg_v = mg_target; mg_step = 0; } else mg_v = v;
    }
    float l = 0, r = 0, a, b;
    if (cur.band >= 0) {
      Stem &st = fight[cur.band];
      if (tt >= cur.start_t) {
        if (cur.gain < 1) cur.gain = fmin(1.0, cur.gain + fin);
        st.at((uint32_t)cur.pos, a, b);
        l += (float)(a * cur.gain); r += (float)(b * cur.gain);
      }
      cur.pos += 1; if (cur.pos >= st.frames) cur.pos -= st.frames;
    }
    if (prev.band >= 0) {
      Stem &st = fight[prev.band];
      if (tt >= prev.start_t) {
        st.at((uint32_t)prev.pos, a, b);
        l += (float)(a * prev.gain); r += (float)(b * prev.gain);
      }
      prev.pos += 1; if (prev.pos >= st.frames) prev.pos -= st.frames;
      prev.gain *= rel;
      if (prev.gain < 1e-4) prev.band = -1;
    }
    if (dn.song >= 0) {
      double p = (tt - dn.t0) * kRate;
      if (p >= 0) {
        uint32_t ip = (uint32_t)p;
        if (ip >= dance[dn.song].frames && !dn.releasing) { /* the song and its tail are over */ }
        else { dance[dn.song].at(ip, a, b); l += (float)(a * dn.gain); r += (float)(b * dn.gain); }
      }
      if (dn.releasing) { dn.gain *= rel; if (dn.gain < 1e-4) dn.song = -1; }
    }
    L[i] += (float)(l * m); R[i] += (float)(r * m);
  }
  now_t = t + n / kRate;
}

extern "C" void music_set_clock(double t) { now_t = t; }

/* kaudio.c: KEYHOLDER's synth, from games/keyholder/src/audio.c (see kaudio.h). Changed from there: the K
 * names, no spoken voice (the page has its own), and kaudio_mix adds into the page's block instead of
 * writing the host's ring. The original's notes follow.
 * audio.c: the synth. No samples. A voice pool for effects (oscillator or
 * noise, an exponential pitch glide, a decay curve, a swept one pole filter,
 * a little FM and an echo send), a continuous voice that follows the game
 * (the staff's charge), a stereo echo bus, and the music: a small arranger
 * that plays one song per mood (bass, string pad, FM keys and bells, a lead,
 * a distorted guitar, a drum kit and a reverb; see "music" below).
 * The synth runs at whatever rate the host asks for, so there is nothing to
 * resample. */
#include "kaudio.h"
#include <math.h>
#include <string.h>
#include <stdbool.h>

#define NV 28
#define TAU 6.28318530718f
enum { W_SINE, W_SQ, W_SAW, W_TRI, W_NOISE };

static float SR = 48000;
static uint32_t rs = 0x12345678u;
static float rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (float)(rs & 0xffffff) / 8388608.f - 1.f; }
static float sgn_frac(float x) { return x - floorf(x); }

static float osc(int w, float ph, float duty) {
  switch (w) {
    case W_SINE: return sinf(ph * TAU);
    case W_SQ: return (ph < duty ? 0.7f : -0.7f) - (2.f * duty - 1.f) * 0.7f;   /* no DC for thin pulses */
    case W_SAW: return ph * 2.f - 1.f;
    case W_TRI: return ph < 0.5f ? ph * 4.f - 1.f : 3.f - ph * 4.f;
    default: return rnd();
  }
}

/* effect voices */
typedef struct {
  bool on; int wave, curve, id;
  float f, mult, dur, age, delay, atk, vol, gl, gr, noise, duty, send;
  float lp, lp1, hp, fm_r, fm_i, ph, mph, lz, hz, drive, wob;
} voice;
static voice vox[NV];

static float pan_clamp(float p) { return p < -1 ? -1 : p > 1 ? 1 : p; }

/* start a voice: wave, start and end frequency, duration, volume, pan */
static voice *vx(int id, int wave, float f0, float f1, float dur, float vol, float pan) {
  int k = 0; float worst = -1;
  for (int i = 0; i < NV; i++) {
    if (!vox[i].on) { k = i; break; }
    float r = vox[i].age / vox[i].dur;
    if (r > worst) { worst = r; k = i; }
  }
  voice *v = &vox[k];
  memset(v, 0, sizeof *v);
  v->on = true; v->id = id; v->wave = wave; v->f = f0; v->dur = dur;
  v->mult = (f0 > 0 && f1 > 0) ? powf(f1 / f0, 1.f / (dur * SR)) : 1.f;
  v->vol = vol; v->curve = 2; v->duty = 0.5f; v->atk = 0.002f;
  float a = (pan_clamp(pan) + 1.f) * 0.78539816f;
  v->gl = cosf(a); v->gr = sinf(a);
  return v;
}

/* echo bus */
#define ECH 32768
static float eb[2][ECH]; static int ew; static float elp[2];

/* music: a small arranger. Each mood is a song: a form (one letter a bar), one
 * chord a bar or two ("IV+V") as roman numerals in the song's key ("^n" moves
 * the key n semitones from there on), a melody in scale degrees with lengths in
 * 16ths ("5:4", ' up an octave, , down, b and # accidentals, '.' a rest, default
 * length 2, '|' checks the bar line), and one part per form letter that picks
 * the drum, bass, piano, guitar and arp patterns and which voices carry the tune.
 * The songs compile to step tables once, at init. */

/* cheap oscillators: a sine table, band limited saw and pulse, a soft clip */
static float sint[2049];
static float fsin(float ph) { ph -= floorf(ph); float x = ph * 2048.f; int i = (int)x; return sint[i] + (sint[i + 1] - sint[i]) * (x - (float)i); }
static float sat(float x) { if (x > 3.f) return 1.f; if (x < -3.f) return -1.f; float x2 = x * x; return x * (27.f + x2) / (27.f + 9.f * x2); }
static float blep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
  if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
  return 0.f;
}
static float bsaw(float ph, float inc) { return 2.f * ph - 1.f - blep(ph, inc); }
static float bpulse(float ph, float inc, float duty) { float p2 = ph + 1.f - duty; p2 -= floorf(p2); return 0.5f * (bsaw(ph, inc) - bsaw(p2, inc)); }
static float adv(float ph, float inc) { ph += inc; return ph >= 1.f ? ph - 1.f : ph; }
static float mtof(float n) { return 440.f * powf(2.f, (n - 69.f) / 12.f); }
static float dk(float tau) { return expf(-1.f / (tau * SR)); }   /* per sample decay for a time constant */
static float lpk(float hz) { float k = hz * TAU / SR; return k > 1.f ? 1.f : k; }

enum { F_PAD = 1, F_LEAD = 2, F_OCT = 4, F_BELL = 8, F_PMEL = 16, F_CRASH = 32, F_NOFILL = 64, F_LEADB = 128, F_TREM = 256 };
enum { LD_SUPER, LD_SQUARE, LD_FLUTE, LD_GTR, LD_SOFT };
enum { K_PIANO, K_EP, K_BELL, K_BOX, K_PLUCK };

/* drums: kick, snare (x, g ghost), hat (x, g soft, o open), cymbal (r ride) */
static const char *DRUMS[] = {
  /* 0 none */    "................" "................" "................" "................",
  /* 1 pop */     "x.......x.x....." "....x.......x..." "x.x.x.x.x.x.x.x." "................",
  /* 2 pop ride */"x.......x.x....." "....x.......x..." "................" "r.r.r.r.r.r.r.r.",
  /* 3 rock */    "x.....x.x.....x." "....x.......x..." "x.x.x.x.x.x.x.x." "................",
  /* 4 rock ride*/"x.....x.x.x...x." "....x.......x..." "................" "r.r.r.r.r.r.r.r.",
  /* 5 euro */    "x...x...x...x..." "....x.......x..." "..o...o...o...o." "................",
  /* 6 euro ride*/"x...x...x...x..." "....x.......x..g" "..o...o...o...o." "r.r.r.r.r.r.r.r.",
  /* 7 lofi */    "x......x..x....." "....x..g.g..x..g" "x.xgx.xgx.xgx.xg" "................",
  /* 8 half */    "x.....x...x....." "........x......." "x.x.x.x.x.x.x.x." "r...r...r...r...",
  /* 9 dbl kick */"xxxxxxxxxxxxxxxx" "....x.......x..." "................" "r...r...r...r...",
  /* 10 brush */  "x.........x....." "........g......." "g.gg.gg.g.gg.gg." "................",
  /* 11 tick */   "x.......x......." "................" "x.g.x.g.x.g.x.g." "................",
  /* 12 city */   "x..x..x...x....." "....x.......x..." "xgxgxgxgxgxgxgxg" "................",
  /* 13 build */  "x...x...x...x..." "x.x.x.x.x.x.x.x." "................" "................",
  /* 14 heart */  "x..x............" "................" "................" "................",
  /* 15 fanfare */"x...x...x...x..." "xxx.x.x.xxx.x.x." "................" "r.r.r.r.r.r.r.r.",
  /* 16 hits */   "x.....x.....x..." "x.....x.....x..." "................" "................",
  /* 17 zone */   "x...x...x...x..." "....x.......x..." "xxoxxxoxxxoxxxox" "................",
  /* 18 snap */   "x.......x......." "....g.......g..." "..x...x...x...x." "................",
  /* 19 rock 16 */"x..x..x...x..x.." "....x.......x..." "xxxxxxxxxxxxxxxx" "................",
};
/* bass: r root, o octave, f fifth, t third, 7 seventh, k the key's tonic, a a step under the next chord, - hold, . off */
static const char *BASS[] = {
  "................", "r---------------", "r.r.r.r.r.r.r.r.", "r-.r-.r-o-.r-.f.", "r.o.r.o.r.o.r.o.",
  "r.r.r.r.r.r.r.ra", "r-----.r--f---.a", "r..o..r.r..o.7..", "k.k.k.k.k.k.k.k.", "r-------f-------",
  "r.rrr.rrr.rrr.rr", "r---t---f---a---", "r-r-r-r-r-r-r-a-", "r-------.---f---", "rro.rro.rro.rro.",
};
/* piano and arp: x the chord, p root and octave low, digits chord tones upward, - hold, . release */
static const char *COMP[] = {
  "................", "x---------------", "x-------x-------", "x--x--x---x--x--", "0.1.2.3.4.3.2.1.",
  "p-------p---p---", "x-----.x---.x-..", "0---2---4-------", "x...x...x...x...", "..x...x...x...x.",
  "0123432101234321", "0-2-4-2-3-2-4-2-", "x-.x-..x-.x-.x-.", "0---4---2-------", "0.1.2.4.0.1.2.4.",
  "x.....x.....x...", "x.x.x.x.x.x.x.x.",
};
/* guitar: x a ringing power chord, m palm muted, - hold, . choke */
static const char *GTR[] = {
  "................", "x---------------", "m.m.m.m.m.m.m.m.", "x--m.mx--m.mx-m.", "m.mmm.mmm.mmm.mm",
  "x-------x-------", "x..x..x.....x...", "x.....x.....x---",
};

typedef struct { char sec; uint8_t drum, bass, pno, gtr, arp; uint16_t fl; } part;
typedef struct {
  float bpm, swing; int key, minor, loop;
  const char *form, *chords, *mel;
  part parts[7];
  uint8_t lead_a, lead_b, pno_k, arp_k, bell_k;
  int8_t lead_oct, pmel_oct, bell_oct, bass_lo, pad_lo, pno_lo, gtr_lo;
  float pad_cut, bass_cut, bass_drive, gain;
  float v_bass, v_pad, v_pno, v_lead, v_gtr, v_drum, v_bell, verb;
} song;

#define P(s, d, b, p, g, a, f) { s, d, b, p, g, a, f }
static const song SONGS[KMOOD_SILENT] = {
  /* TITLE: D major, 140. A piano hook in octaves, then verse, a royal road prechorus and a
   * big chorus on IV V iii vi with a supersaw lead; out on bVI bVII I */
  [KMOOD_TITLE] = { 140, 0, 62, 0, 0,
    "iiii" "aaaaaaaa" "bbbbbbbb" "cccccccccccccccc" "tttt",
    "IVM7 V iii7 vi  I V vi iii IV I ii7 V  IVM7 V iii7 vi IVM7 V bVI bVII "
    "IVM7 V iii7 vi IVM7 V I I7 IVM7 V iii7 vi ii7 iii7 IVM7 V  bVIM7 bVII I I",
    "1':3 7:3 1':2 2':4 3':4 | 2':3 1':3 7:2 5:6 .:2 | 7:3 1':3 2':2 3':6 2':2 | 1':4 7:2 6:2 6:8 |"
    "3 3 3 2 3:4 5:4 | 5:3 4:3 3:2 2:8 | 1 1 1 7, 1:4 3:4 | 3:6 2:2 2:8 | 4 4 4 3 4:4 6:4 | 5:3 3:3 1:2 1:8 | 2 3 4 5 6:4 5:4 | 5:12 .:4 |"
    "6:4 5:4 6:2 1':6 | 7:4 6:4 5:8 | 5:4 3:4 5:2 7:6 | 6:12 .:4 | 6:4 5:4 6:2 1':6 | 2':4 1':4 7:8 | 1':6 b7:2 1':8 | 2':12 .:4 |"
    "1':3 7:3 1':2 2':4 3':4 | 2':3 1':3 7:2 5:6 .:2 | 7:3 1':3 2':2 3':6 2':2 | 1':4 7:2 6:2 6:8 |"
    "1':3 7:3 1':2 2':4 3':4 | 2':3 3':3 4':2 5':8 | 5':3 3':3 2':2 1':8 | .:4 5 6 1' 2' 3' 4' |"
    "1':3 7:3 1':2 2':4 3':4 | 2':3 1':3 7:2 5:6 .:2 | 7:3 1':3 2':2 3':4 5':4 | 6':4 5':4 3':4 2':4 |"
    "1':3 2':3 3':2 4':4 3':4 | 3':6 2':2 1':4 7:4 | 1':3 7:3 6:2 5:8 | 2':12 .:4 |"
    "1':8 .:8 | 2':8 .:4 1' 2' | 3':12 .:4 | .:16 |",
    { P('i', 0, 0, 1, 0, 0, F_PAD | F_OCT), P('a', 1, 12, 3, 0, 0, F_PAD | F_LEAD | F_LEADB),
      P('b', 1, 3, 3, 5, 14, F_PAD | F_LEAD | F_LEADB), P('c', 2, 3, 3, 1, 0, F_PAD | F_LEAD | F_BELL | F_CRASH),
      P('t', 16, 1, 1, 1, 0, F_PAD | F_OCT) },
    LD_SUPER, LD_SQUARE, K_PIANO, K_BELL, K_BELL, 0, -12, 12, 36, 55, 55, 40,
    2600, 900, 0, 1.0f, 0.8f, 0.5f, 0.6f, 0.5f, 0.35f, 0.8f, 0.3f, 0.3f },

  /* EXPLORE: G major, 116, a light swing. City pop on Main Street: an electric piano, a slap of
   * octave bass, a whistle of a lead, the Just The Two Of Us turn in B, the royal road in C */
  [KMOOD_EXPLORE] = { 116, 0.06f, 67, 0, 0,
    "aaaaaaaa" "bbbbbbbb" "AAAAAAAA" "cccccccc" "tttt",
    "IM7 iii7 IVM7 V7 IM7 iii7 ii7 V7sus  IVM7 III7 vi7 v7+I7 IVM7 III7 vi7 V7sus "
    "IM7 iii7 IVM7 V7 IM7 iii7 ii7 V7sus  IVM7 V7 iii7 vi7 ii7 V7 IM7 IM7  IVM7 iv7 IM7 V7sus",
    "5 6 5 3 5:6 .:2 | 3 2 3 5 7:6 .:2 | 6:3 5:3 3:2 6:4 5:4 | 4:3 3:3 2:2 5,:8 | 5 6 5 3 5:4 1':4 | 7:4 6 5 6:8 | 6:3 5:3 4:2 3:4 2:4 | 2:12 .:4 |"
    "3:3 5:3 7:2 7:8 | #5:3 7:3 2':2 2':8 | 1':3 7:3 6:2 5:8 | 4:4 b7:4 b7:2 6:2 5:4 | 3:3 5:3 7:2 7:8 | 2':3 1':3 7:2 #5:8 | 6:4 7:4 1':4 2':4 | 2':12 .:4 |"
    "5 6 5 3 5:6 .:2 | 3 2 3 5 7:6 .:2 | 6:3 5:3 3:2 6:4 5:4 | 4:3 3:3 2:2 5,:8 | 5 6 5 3 5:4 1':4 | 7:4 6 5 6:8 | 6:3 5:3 4:2 3:4 2:4 | 2:12 .:4 |"
    "1':3 7:3 5:2 7:4 1':4 | 2':3 1':3 6:2 4:8 | 7:3 6:3 3:2 5:4 6:4 | 5:12 .:4 | 1':3 7:3 6:2 1':4 2':4 | 3':6 2':2 7:8 | 7:3 5:3 3:2 5:8 | 1':12 .:4 |"
    "6:8 5:8 | b6:8 5:8 | 7:8 5:8 | 2:12 .:4 |",
    { P('a', 12, 7, 12, 0, 0, F_LEAD), P('b', 12, 7, 12, 0, 14, F_PAD | F_LEAD),
      P('A', 12, 7, 12, 0, 0, F_LEAD | F_BELL), P('c', 1, 3, 3, 0, 0, F_PAD | F_LEAD | F_LEADB | F_BELL | F_CRASH),
      P('t', 18, 11, 1, 0, 0, F_PAD | F_LEAD) },
    LD_FLUTE, LD_SQUARE, K_EP, K_BELL, K_BELL, 0, 0, 12, 36, 55, 55, 40,
    2400, 1100, 0, 1.0f, 0.8f, 0.6f, 0.8f, 0.3f, 0, 0.75f, 0.25f, 0.25f },

  /* FIGHT: A minor, 168. J-rock into eurobeat: a guitar lead riff, palm muted chugs, octave bass,
   * a circle of fifths prechorus, a royal road chorus, then the chorus again a whole step up */
  [KMOOD_FIGHT] = { 168, 0, 69, 1, 4,
    "iiii" "aaaaaaaa" "bbbbbbbb" "cccccccc" "dddddddd" "tttt",
    "i VI VII V  i i VI VII i i iv V  iv VII III VI iio V V V  VI VII v i VI VII i i "
    "^2 VI VII v i VI VII i i  ^0 VI VII V V",
    "1':2 5:2 1':2 2':2 3':4 2':2 1':2 | 7:2 5:2 7:2 1':2 2':4 1':2 7:2 | 6:2 4:2 6:2 7:2 1':4 7:2 6:2 | 5:2 #7:2 2':2 #7:2 5:2 #7:2 2':2 #7:2 |"
    "1:3 1:3 3:2 5:4 4:2 3:2 | 2:3 3:3 2:2 1:8 | 1:3 1:3 3:2 6:4 5:2 4:2 | 5:3 4:3 2:2 4:8 | 1:3 1:3 3:2 5:4 4:2 3:2 | 2:3 3:3 5:2 1':8 | 1':4 6:4 4:4 1':4 | 2':4 #7:12 |"
    "1':3 6:3 4:2 6:8 | 7:3 4:3 2:2 4:8 | 5:3 3:3 7,:2 3:8 | 6:3 3:3 1:2 3:8 | 4:3 2:3 6,:2 2:8 | #7,:4 2:4 5:4 #7:4 | 2':16 | 3':8 2':4 #7:4 |"
    "1':3 3':3 1':2 7:4 6:4 | 7:3 2':3 7:2 1':4 2':4 | 2':3 7:3 5:2 7:4 1':4 | 1':12 .:2 5:2 | 1':3 3':3 1':2 7:4 6:4 | 7:3 2':3 3':2 4':8 | 3':4 2':4 1':4 7:4 | 1':12 .:4 |"
    "1':3 3':3 1':2 7:4 6:4 | 7:3 2':3 7:2 1':4 2':4 | 2':3 7:3 5:2 7:4 1':4 | 1':12 .:2 5:2 | 1':3 3':3 1':2 7:4 6:4 | 7:3 2':3 3':2 4':8 | 3':4 2':4 1':4 7:4 | 1':12 .:4 |"
    "6:4 7:4 1':4 2':4 | 2':4 1':4 7:4 5:4 | #7:16 | 5:4 #7:4 2':4 #7:4 |",
    { P('i', 16, 2, 0, 7, 0, F_LEAD), P('a', 3, 2, 0, 2, 0, F_LEAD),
      P('b', 13, 3, 5, 5, 0, F_PAD | F_LEAD | F_LEADB), P('c', 6, 4, 3, 3, 0, F_PAD | F_LEAD | F_LEADB | F_CRASH),
      P('d', 6, 4, 3, 3, 0, F_PAD | F_LEAD | F_LEADB | F_BELL | F_CRASH), P('t', 16, 2, 5, 7, 0, F_LEAD) },
    LD_GTR, LD_SUPER, K_PIANO, K_BELL, K_BELL, -12, -12, 0, 33, 55, 52, 40,
    2800, 1400, 0.3f, 1.0f, 0.85f, 0.45f, 0.5f, 0.6f, 0.6f, 0.9f, 0.25f, 0.18f },

  /* BOSS: D minor, 150. Phrygian weight: bII everywhere, a low gallop, double kick, a guitar
   * lead in the low register, a half time chorus that drops on i VI III VII */
  [KMOOD_BOSS] = { 150, 0, 62, 1, 4,
    "iiii" "aaaaaaaa" "bbbbbbbb" "cccccccc" "AAAAAAAA" "tttt",
    "i i bII i  i bII i bII VI VII V V  iv iv i i VI bII V V  i VI III VII i VI bII V "
    "i bII i bII VI VII V V  i bII VI V",
    "1:2 1:2 b2:2 1:2 5:4 4:2 b2:2 | 1:2 1:2 b2:2 1:2 #7,:8 | b2:2 b2:2 3:2 b2:2 6:4 5:2 3:2 | 1:16 |"
    "5:6 6:2 5:4 4:4 | 3:6 4:2 3:4 b2:4 | 5:6 6:2 5:4 1':4 | b2':8 6:8 | 6:4 5:4 4:4 3:4 | 5:4 4:4 2:4 4:4 | 5:8 #7:8 | 2':8 5:4 #7:4 |"
    "4:4 6:4 1':8 | 2':4 1':4 6:8 | 1':4 5:4 3:8 | 2:4 3:4 5:8 | 6:4 1':4 3':8 | b2':4 6:4 4:8 | #7:8 2':8 | 5':12 .:4 |"
    "1':6 5:2 1':4 2':4 | 3':6 2':2 1':4 6:4 | 1':6 7:2 5:4 3:4 | 4:8 5:4 7:4 | 1':6 5:2 1':4 2':4 | 3':6 4':2 3':4 2':4 | b2':6 1':2 6:8 | #7:12 .:4 |"
    "5:6 6:2 5:4 4:4 | 3:6 4:2 3:4 b2:4 | 5:6 6:2 5:4 1':4 | b2':8 6:8 | 6:4 5:4 4:4 3:4 | 5:4 4:4 2:4 4:4 | 5:8 #7:8 | 2':8 5:4 #7:4 |"
    "1':8 b2':8 | 1':16 | 6:8 5:8 | #7:16 |",
    { P('i', 16, 10, 5, 6, 0, F_LEAD), P('a', 19, 10, 0, 4, 0, F_LEAD | F_PAD),
      P('b', 9, 5, 5, 5, 0, F_PAD | F_LEAD), P('c', 8, 9, 5, 1, 0, F_PAD | F_LEAD | F_LEADB | F_CRASH),
      P('A', 19, 10, 5, 4, 0, F_LEAD | F_PAD), P('t', 9, 10, 5, 6, 0, F_LEAD) },
    LD_GTR, LD_SUPER, K_PIANO, K_BELL, K_BELL, 0, -12, 12, 33, 50, 48, 38,
    1800, 900, 0.8f, 1.0f, 0.6f, 0.55f, 0.5f, 0.55f, 0.65f, 0.9f, 0.2f, 0.18f },

  /* CLEAR: C major, 140. A four bar fanfare (snare ruffs, bVI bVII I), then a gentle results loop */
  [KMOOD_CLEAR] = { 140, 0, 60, 0, 4,
    "ssss" "aaaaaaaa" "bbbbbbbb",
    "I I bVI+bVII I  IM7 vi7 IVM7 V7 IM7 vi7 ii7 V7  IVM7 V iii7 vi IVM7 V7 IM7 IM7",
    "5:1 .:1 5:1 .:1 5:1 .:1 1':8 .:2 | 7:2 1':2 2':2 3':10 | b3':8 4':8 | 5':12 .:4 |"
    "3 5 1':4 7:4 5:4 | 6 5 3:4 5:8 | 4 6 1':4 3':4 1':4 | 2':12 .:4 | 3 5 1':4 7:4 5:4 | 6 1' 3':4 2':8 | 1':4 6:4 4:4 6:4 | 5:12 .:4 |"
    "6:4 5:4 3:4 5:4 | 7:4 6:4 5:8 | 5:4 3:4 7:4 5:4 | 6:12 .:4 | 1':4 7:4 6:4 1':4 | 2':4 1':4 7:4 5:4 | 1':16 | .:16 |",
    { P('s', 15, 2, 8, 1, 0, F_PAD | F_LEAD | F_CRASH), P('a', 18, 7, 12, 0, 0, F_PAD | F_LEAD | F_LEADB),
      P('b', 18, 7, 12, 0, 14, F_PAD | F_LEAD | F_LEADB | F_BELL) },
    LD_SUPER, LD_FLUTE, K_EP, K_BELL, K_BELL, 0, 0, 24, 36, 55, 55, 40,
    2600, 1000, 0, 1.0f, 0.8f, 0.45f, 0.7f, 0.35f, 0.3f, 0.75f, 0.25f, 0.28f },

  /* CINE: C major, 70. Out of the way of the voices: a dark string pad, a bass note a bar, one
   * high piano note at a time */
  [KMOOD_CINE] = { 70, 0, 72, 0, 0,
    "aaaaaaaa" "bbbbbbbb" "aaaaaaaa" "cccccccc",
    "IM7 IVM7 vi7 IVM7 IM7 IVM7 ii7 V7sus  vi7 iii7 IVM7 IM7 ii7 iii7 IVM7 V7sus "
    "IM7 IVM7 vi7 IVM7 IM7 IVM7 ii7 V7sus  IVM7 V iii7 vi IVM7 V IM7 IM7",
    "5:12 .:4 | 6:8 5:8 | 3:16 | 2:12 .:4 | 5:12 .:4 | 6:8 1':8 | 7:16 | 5:12 .:4 |"
    "1':16 | 7:8 5:8 | 6:12 .:4 | 5:16 | 1':8 2':8 | 3':8 2':8 | 1':16 | 2':12 .:4 |"
    "5:12 .:4 | 6:8 5:8 | 3:16 | 2:12 .:4 | 5:12 .:4 | 6:8 1':8 | 7:16 | 5:12 .:4 |"
    "1':8 7:8 | 2':8 1':8 | 7:8 5:8 | 3:16 | 1':8 7:8 | 2':8 3':8 | 2':16 | 1':12 .:4 |",
    { P('a', 0, 1, 13, 0, 0, F_PAD | F_PMEL), P('b', 0, 1, 7, 0, 0, F_PAD | F_PMEL),
      P('c', 0, 1, 13, 0, 0, F_PAD | F_PMEL) },
    LD_SOFT, LD_SOFT, K_PIANO, K_PIANO, K_BELL, 0, 0, 12, 36, 52, 48, 40,
    1100, 500, 0, 0.75f, 0.6f, 0.7f, 0.45f, 0, 0, 0, 0, 0.4f },

  /* HOSTING: F major, 86, swung 16ths. Lo-fi slice of life: a Rhodes on sevenths, a lazy kick and
   * ghost notes, a soft lead, a glockenspiel on the second time round, a borrowed iv in the B part */
  [KMOOD_HOSTING] = { 86, 0.16f, 65, 0, 0,
    "aaaaaaaa" "bbbbbbbb" "AAAAAAAA" "cccccccc" "tttt",
    "IVM7 iii7 ii7 IM7 IVM7 iii7 vi7 V7sus  ii7 V7 IM7 vi7 ii7 V7 IM7 I7 "
    "IVM7 iii7 ii7 IM7 IVM7 iii7 vi7 V7sus  IVM7 iv7 iii7 VI7 ii7 V7 IM7 V7sus  IVM7 iii7 ii7 V7sus",
    "3:3 5:3 6:2 5:8 | 3:3 2:3 1:2 3:8 | 2:3 4:3 6:2 1':8 | 7:6 6:2 5:8 | 3:3 5:3 6:2 1':4 6:4 | 5:3 3:3 2:2 3:8 | 1:3 3:3 5:2 6:8 | 5:12 .:4 |"
    "4:3 5:3 6:2 1':4 6:4 | 7:6 5:2 5:8 | 3:3 5:3 7:2 1':8 | 6:6 5:2 3:8 | 4:3 5:3 6:2 1':4 2':4 | 3':6 2':2 7:8 | 1':3 7:3 5:2 3:8 | b7:6 6:2 5:8 |"
    "3:3 5:3 6:2 5:8 | 3:3 2:3 1:2 3:8 | 2:3 4:3 6:2 1':8 | 7:6 6:2 5:8 | 3:3 5:3 6:2 1':4 6:4 | 5:3 3:3 2:2 3:8 | 1:3 3:3 5:2 6:8 | 5:12 .:4 |"
    "6:3 1':3 3':2 2':8 | 1':6 b6:2 4:8 | 7:3 5:3 3:2 5:8 | 6:3 #1':3 3':2 5:8 | 4:3 6:3 1':2 7:8 | 6:6 5:2 3:8 | 3:3 2:3 1:2 3:8 | 5:12 .:4 |"
    "6:8 5:8 | 5:8 3:8 | 4:8 6:8 | 5:12 .:4 |",
    { P('a', 7, 6, 6, 0, 0, F_LEAD), P('b', 7, 6, 6, 0, 0, F_PAD | F_LEAD),
      P('A', 7, 6, 6, 0, 0, F_LEAD | F_BELL), P('c', 7, 11, 6, 0, 0, F_PAD | F_LEAD | F_BELL),
      P('t', 18, 1, 1, 0, 0, F_PAD) },
    LD_SOFT, LD_SOFT, K_EP, K_BELL, K_BOX, 0, 0, 12, 36, 55, 53, 40,
    1800, 700, 0, 1.0f, 0.85f, 0.5f, 0.85f, 0.32f, 0, 0.8f, 0.22f, 0.3f },

  /* TENSION: D minor, 96. A pedal D in eighths, a trembling pad, a clock tick, a low flute motif
   * with long rests, Eb (bII) leaning on the tonic */
  [KMOOD_TENSION] = { 96, 0, 62, 1, 0,
    "aaaaaaaa" "bbbbbbbb" "aaaaaaaa" "cccccccc",
    "i i VI VI iv iv V V  i bII i bII VI VII V V  i i VI VI iv iv V V  iv iv VI VI bII bII V V",
    "5:4 6:4 5:8 | .:16 | 5:4 6:4 1':8 | .:16 | 4:4 3:4 1:8 | .:16 | #7,:8 1:4 2:4 | 2:16 |"
    "1':6 b2':2 1':8 | .:16 | 1':6 b2':2 3':8 | .:16 | 6:8 5:8 | 4:8 2:8 | #7:16 | 2':8 1':4 #7:4 |"
    "5:4 6:4 5:8 | .:16 | 5:4 6:4 1':8 | .:16 | 4:4 3:4 1:8 | .:16 | #7,:8 1:4 2:4 | 2:16 |"
    "4:8 6:8 | 1':16 | 3':8 1':8 | 6:16 | b2':8 6:8 | 4:16 | #7:8 2':8 | 5:16 |",
    { P('a', 11, 8, 5, 0, 0, F_PAD | F_TREM | F_LEAD), P('b', 11, 8, 5, 0, 0, F_PAD | F_TREM | F_LEAD),
      P('c', 14, 2, 8, 0, 0, F_PAD | F_TREM | F_LEAD) },
    LD_FLUTE, LD_FLUTE, K_PIANO, K_BELL, K_BELL, 0, -12, 12, 33, 50, 45, 40,
    1300, 600, 0, 0.9f, 0.85f, 0.9f, 0.6f, 0.3f, 0, 0.6f, 0, 0.35f },

  /* NIGHT: Eb major, 72. After ten: a music box carries the tune, piano broken chords, brushes */
  [KMOOD_NIGHT] = { 72, 0.1f, 63, 0, 0,
    "aaaaaaaa" "bbbbbbbb" "AAAAAAAA" "cccccccc",
    "IM7 IVM7 iii7 vi7 ii7 V7sus IM7 IVM7  IVM7 iv7 iii7 VI7 ii7 V7 IM7 V7sus "
    "IM7 IVM7 iii7 vi7 ii7 V7sus IM7 IVM7  vi7 iii7 IVM7 IM7 ii7 iii7 IVM7 V7sus",
    "3:4 5:4 7:8 | 6:4 5:4 3:8 | 5:4 3:4 2:8 | 1:12 .:4 | 2:4 4:4 6:8 | 5:4 4:4 2:8 | 3:4 2:4 1:8 | 3:12 .:4 |"
    "1':4 7:4 6:8 | b6:4 5:4 4:8 | 5:4 7:4 2':8 | 6:4 b2':4 3':8 | 2':4 1':4 6:8 | 7:4 5:4 4:8 | 3:4 5:4 1':8 | 2':12 .:4 |"
    "3:4 5:4 7:8 | 6:4 5:4 3:8 | 5:4 3:4 2:8 | 1:12 .:4 | 2:4 4:4 6:8 | 5:4 4:4 2:8 | 3:4 2:4 1:8 | 3:12 .:4 |"
    "1':4 3':4 2':8 | 7:4 5:4 3:8 | 6:4 1':4 3':8 | 2':12 .:4 | 6:4 4:4 2:8 | 7:4 5:4 3:8 | 1':4 7:4 6:8 | 5:12 .:4 |",
    { P('a', 0, 13, 11, 0, 0, F_PAD | F_BELL), P('b', 10, 13, 11, 0, 0, F_PAD | F_BELL),
      P('A', 10, 13, 11, 0, 0, F_PAD | F_BELL), P('c', 10, 13, 11, 0, 0, F_PAD | F_BELL | F_PMEL) },
    LD_SOFT, LD_SOFT, K_PIANO, K_BELL, K_BOX, 0, 0, 12, 36, 51, 51, 40,
    1300, 600, 0, 0.8f, 0.7f, 0.5f, 0.55f, 0, 0, 0.5f, 0.45f, 0.4f },

  /* ZONE: E minor, 132. Arcade: a square lead, a sixteenth pluck arpeggio, four on the floor */
  [KMOOD_ZONE] = { 132, 0, 64, 1, 0,
    "aaaaaaaa" "bbbbbbbb" "AAAAAAAA" "cccccccc",
    "i VI III VII i VI iv V  VI VII v i VI VII V V  i VI III VII i VI iv V  i v VI III iv i iv V",
    "1':2 5:2 3:2 5:2 1':2 2':2 3':4 | 3':2 1':2 6:2 1':2 3':4 2':4 | 2':2 7:2 5:2 7:2 2':4 1':4 | 7:4 2':4 4':8 | 1':2 5:2 3:2 5:2 1':2 2':2 3':4 | 3':2 1':2 6:2 1':2 5':4 4':4 | 3':4 1':4 6:4 4:4 | #7:4 2':4 5:8 |"
    "3':3 3':3 2':2 1':4 2':4 | 2':3 2':3 1':2 7:8 | 1':3 2':3 4':2 5':8 | 3':12 .:4 | 3':3 3':3 2':2 1':4 2':4 | 4':3 3':3 2':2 1':8 | #7:4 2':4 5':8 | 2':8 #7:8 |"
    "1':2 5:2 3:2 5:2 1':2 2':2 3':4 | 3':2 1':2 6:2 1':2 3':4 2':4 | 2':2 7:2 5:2 7:2 2':4 1':4 | 7:4 2':4 4':8 | 1':2 5:2 3:2 5:2 1':2 2':2 3':4 | 3':2 1':2 6:2 1':2 5':4 4':4 | 3':4 1':4 6:4 4:4 | #7:4 2':4 5:8 |"
    "5 1' 3' 1' 5 1' 3' 5' | 5 7 2' 7 5 7 2' 5' | 6 1' 3' 1' 6 1' 3' 6' | 3 5 7 5 3 5 7 3' | 4 6 1' 6 4 6 1' 4' | 1':4 7:4 5:4 3:4 | 4:4 6:4 1':4 3':4 | #7:8 2':8 |",
    { P('a', 17, 14, 0, 0, 10, F_LEAD), P('b', 5, 4, 3, 0, 10, F_PAD | F_LEAD),
      P('A', 17, 14, 9, 0, 10, F_PAD | F_LEAD), P('c', 17, 14, 9, 0, 10, F_PAD | F_LEAD | F_CRASH) },
    LD_SQUARE, LD_SQUARE, K_PIANO, K_PLUCK, K_BELL, 0, 0, 12, 36, 55, 55, 40,
    2600, 1300, 0, 1.0f, 0.85f, 0.45f, 0.4f, 0.5f, 0, 0.8f, 0.45f, 0.18f },

  /* FINAL: E major, 140. Burnout, your own tired self: bittersweet borrowed iv, a piano octave
   * hook, a sad bridge on bVI bVII, the last chorus a half step up */
  [KMOOD_FINAL] = { 140, 0, 64, 0, 0,
    "iiii" "aaaaaaaa" "bbbbbbbb" "cccccccc" "dddddddd" "eeeeeeee" "tttt",
    "IV iv I I  I iii IV iv I vi ii7 V  vi IV I V vi IV bVII V  IVM7 V iii7 vi IVM7 iv I I "
    "vi V IV I bVI bVII V V  ^1 IVM7 V iii7 vi IVM7 iv I I  ^0 bVIM7 bVII I I",
    "3:3 5:3 6:2 1':8 | 1':6 b6:2 4:8 | 3:6 2:2 1:8 | .:16 |"
    "1:4 2:4 3:6 5:2 | 5:6 3:2 3:8 | 6:4 5:4 6:2 1':6 | 1':6 b6:2 b6:8 | 5:4 3:4 1:6 2:2 | 3:6 1:2 1:8 | 2:4 3:4 4:4 6:4 | 5:12 .:4 |"
    "6:4 6:2 7:2 1':8 | 1':4 7:4 6:8 | 5:4 6:4 7:4 1':4 | 2':16 | 6:4 6:2 7:2 1':8 | 1':4 2':4 3':8 | 4':8 2':8 | 7:12 .:4 |"
    "3:3 5:3 6:2 1':4 7:4 | 7:3 6:3 5:2 2:8 | 3:3 5:3 7:2 2':4 7:4 | 1':3 7:3 6:2 6:8 | 3:3 5:3 6:2 1':4 3':4 | 1':6 b6:2 4:8 | 3:6 2:2 1:8 | .:8 5 6 7 1' |"
    "1':8 7:4 6:4 | 7:8 6:4 5:4 | 6:8 5:4 3:4 | 3:12 2 3 | b6:8 5:4 1':4 | b7:8 6:4 2':4 | 2':8 3':4 4':4 | 5':12 .:4 |"
    "3:3 5:3 6:2 1':4 7:4 | 7:3 6:3 5:2 2:8 | 3:3 5:3 7:2 2':4 7:4 | 1':3 7:3 6:2 6:8 | 3:3 5:3 6:2 1':4 3':4 | 1':6 b6:2 4:8 | 3:6 2:2 1:8 | .:8 5 6 7 1' |"
    "1':8 5:8 | 2':16 | 1':16 | .:16 |",
    { P('i', 0, 0, 1, 0, 0, F_PAD | F_OCT), P('a', 14, 1, 4, 0, 0, F_PAD | F_LEAD | F_LEADB),
      P('b', 1, 12, 3, 5, 0, F_PAD | F_LEAD | F_LEADB), P('c', 4, 3, 3, 1, 0, F_PAD | F_LEAD | F_BELL | F_CRASH),
      P('d', 8, 9, 1, 0, 14, F_PAD | F_OCT), P('e', 6, 4, 3, 3, 0, F_PAD | F_LEAD | F_BELL | F_OCT | F_CRASH),
      P('t', 16, 1, 1, 1, 0, F_PAD | F_OCT) },
    LD_SUPER, LD_FLUTE, K_PIANO, K_BELL, K_BELL, 0, -12, 12, 36, 55, 52, 40,
    2800, 1000, 0, 1.0f, 0.8f, 0.55f, 0.55f, 0.5f, 0.4f, 0.8f, 0.25f, 0.3f },
};
#undef P

/* the compiled songs */
#define MAXBAR 64
typedef struct { int8_t root; uint8_t t, f, s, a; } chord_t;   /* root from the song key; third, fifth, seventh (0 none), added ninth */
static chord_t CH[KMOOD_SILENT][MAXBAR * 2];
static int8_t MON[KMOOD_SILENT][MAXBAR * 16];    /* note on (semitones from the key), -128 none */
static uint8_t MGATE[KMOOD_SILENT][MAXBAR * 16];
static int8_t BMOD[KMOOD_SILENT][MAXBAR];
static int8_t BSEC[KMOOD_SILENT][MAXBAR];        /* the bar's index in its section */
static uint8_t NB[KMOOD_SILENT];
static char song_err[200];
static const int SCALE[2][7] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}};

static void serr(int si, const char *what, int at) {
  if (song_err[0]) return;
  const char *d = "0123456789";
  char *o = song_err; *o++ = 'm'; *o++ = d[si / 10]; *o++ = d[si % 10]; *o++ = ' ';
  while (*what && o < song_err + 180) *o++ = *what++;
  *o++ = ' '; *o++ = d[(at / 100) % 10]; *o++ = d[(at / 10) % 10]; *o++ = d[at % 10]; *o = 0;
}
static const char *parse_chord(const char *p, int minor, int mod, chord_t *c) {
  int acc = 0, val = 0, prev = 0;
  while (*p == 'b' || *p == '#') acc += *p++ == 'b' ? -1 : 1;
  int up = *p == 'I' || *p == 'V';
  for (;; p++) {
    int v = (*p == 'I' || *p == 'i') ? 1 : (*p == 'V' || *p == 'v') ? 5 : 0;
    if (!v) break;
    val += v; if (v > prev && prev) val -= 2 * prev; prev = v;
  }
  if (val < 1 || val > 7) val = 1;
  c->root = (int8_t)(SCALE[minor][val - 1] + acc + mod); c->t = up ? 4 : 3; c->f = 7; c->s = 0; c->a = 0;
  for (;;) {
    if (p[0] == 'M' && p[1] == '7') { c->s = 11; p += 2; }
    else if (p[0] == '7') { c->s = c->f == 6 ? 9 : 10; p++; }
    else if (p[0] == 's' && p[1] == 'u' && p[2] == 's') { c->t = 5; p += 3; }
    else if (p[0] == 'o') { c->t = 3; c->f = 6; p++; }
    else if (p[0] == '9') { c->a = 14; p++; }
    else break;
  }
  return p;
}
static int pint(const char **pp) {
  const char *p = *pp; int s = 1, v = 0;
  if (*p == '-') { s = -1; p++; }
  while (*p >= '0' && *p <= '9') v = v * 10 + *p++ - '0';
  *pp = p; return v * s;
}
static void song_build(int si) {
  const song *S = &SONGS[si];
  int nb = (int)strlen(S->form); if (nb > MAXBAR) { serr(si, "too many bars", nb); nb = MAXBAR; }
  NB[si] = (uint8_t)nb;
  for (int b = 0; b < nb; b++) BSEC[si][b] = (int8_t)((b > 0 && S->form[b - 1] == S->form[b]) ? BSEC[si][b - 1] + 1 : 0);
  const char *p = S->chords; int bar = 0, mod = 0;
  while (*p) {
    if (*p == ' ') { p++; continue; }
    if (*p == '^') { p++; mod = pint(&p); continue; }
    chord_t a, b;
    p = parse_chord(p, S->minor, mod, &a); b = a;
    if (*p == '+') p = parse_chord(p + 1, S->minor, mod, &b);
    if (*p && *p != ' ') { serr(si, "chord junk at bar", bar); while (*p && *p != ' ') p++; }
    if (bar < nb) { CH[si][bar * 2] = a; CH[si][bar * 2 + 1] = b; BMOD[si][bar] = (int8_t)mod; }
    bar++;
  }
  if (bar != nb) serr(si, "chord count", bar);
  memset(MON[si], -128, sizeof MON[si]); memset(MGATE[si], 0, sizeof MGATE[si]);
  p = S->mel; int pos = 0;
  while (*p) {
    if (*p == ' ') { p++; continue; }
    if (*p == '|') { if (pos % 16) serr(si, "bar line off at bar", pos / 16); p++; continue; }
    int rest = 0, acc = 0, deg = 1, oct = 0;
    if (*p == '.') { rest = 1; p++; }
    else {
      while (*p == 'b' || *p == '#') acc += *p++ == 'b' ? -1 : 1;
      if (*p >= '1' && *p <= '7') deg = *p++ - '0'; else { serr(si, "melody junk at bar", pos / 16); p++; continue; }
      while (*p == '\'' || *p == ',') oct += *p++ == '\'' ? 1 : -1;
    }
    int d = 2;
    if (*p == ':') { p++; d = pint(&p); if (d < 1) d = 1; }
    int b = pos / 16; if (b >= nb) b = nb - 1;
    int n = SCALE[S->minor][deg - 1] + acc + oct * 12 + BMOD[si][b];
    for (int k = 0; k < d && pos + k < MAXBAR * 16; k++) {
      MON[si][pos + k] = (int8_t)(k == 0 && !rest ? n : -128);
      MGATE[si][pos + k] = (uint8_t)!rest;
    }
    pos += d;
  }
  if (pos != nb * 16) serr(si, "melody length at bar", pos / 16);
}

/* ---- the band ---- */
typedef struct { float ph, mph, inc, ratio, amp, adec, rel, idx, idec, gl, gr, snd; int grp; bool on, held, sq; } pvoice;
#define NPV 18
static pvoice pv[NPV];
static struct { float inc, ph, amp, gate, env, z1, z2; } bs;
static struct { float inc[4], ph[8], amp[4], on[4], gate, lvl, zl1, zl2, zr1, zr2, tph; } pad;
static struct { float f, ft, ph[3], amp, gate, vt, z1, zr, z2, z3; int kind; } ld;
static struct { float inc[3], ph[6], amp, dec, gate; bool mute; float zl1, zl2, zr1, zr2, hl, hr; } gt;
static struct { float kph, kenv, kp, kclick, senv, sph, sz, sv, henv, hz, hdec, hv, renv, rph[3], rz, cenv, cz, cz2, tenv, tph, tf; } dm;
static float kdec, kpdec, sdec, hdec_c, hdec_o, rdec, cdec, tdec;
/* a small mono in, stereo out reverb for the band */
#define RVC 2400
static float rc[4][RVC], ra[4][1200], rcz[4];
static int rcl[4], ral[4], rci[4], rai[4];
static int cur = -1, want = KMOOD_SILENT;
static float mgain, mtarget = 1, music_on = 1;
static double sq_ctr; static int sq_bar, sq_st;
static char gt_last = 'x', pn_last = '.';
static uint16_t cur_fl;   /* the playing part's flags */
static float st_len;
static float pad_k, gtk_open, gtk_mute, gthp, ldk, bsdrive;

/* tests can mute parts of the band: 1 bass, 2 pad, 4 keys, 8 lead, 16 guitar, 32 drums, 64 reverb */
#ifdef KH_AUDIO_STEMS
static unsigned stem_mute;
#define STEM(b) (!(stem_mute & (b)))
#else
#define STEM(b) 1
#endif
static int place(int pc, int lo) { int n = lo + (((pc - lo) % 12) + 12) % 12; return n; }
static const part *part_of(const song *S, char c) {
  for (int i = 0; i < 7 && S->parts[i].sec; i++) if (S->parts[i].sec == c) return &S->parts[i];
  return &S->parts[0];
}

static void pv_off(int grp) { for (int i = 0; i < NPV; i++) if (pv[i].on && pv[i].grp == grp) pv[i].held = false; }
static void pv_on(int grp, float note, int kind, float vel, float pan) {
  int k = 0; float q = 1e9f;
  for (int i = 0; i < NPV; i++) { float w = pv[i].on ? pv[i].amp + (pv[i].held ? 1.f : 0.f) : -1.f; if (w < q) { q = w; k = i; } }
  pvoice *v = &pv[k];
  float f = mtof(note);
  v->on = true; v->held = true; v->grp = grp; v->ph = 0; v->mph = 0; v->inc = f / SR; v->sq = false;
  float hi = note > 72 ? (note - 72) / 24.f : 0;   /* high notes die sooner */
  switch (kind) {
    case K_PIANO: v->ratio = 1.f; v->idx = 0.32f; v->idec = dk(0.22f); v->adec = dk(1.6f - hi); v->rel = dk(0.14f); v->snd = 0.35f; vel *= 0.9f; break;
    case K_EP:    v->ratio = 1.f; v->idx = 0.2f; v->idec = dk(0.45f); v->adec = dk(2.2f - hi); v->rel = dk(0.12f); v->snd = 0.3f; break;
    case K_BELL:  v->ratio = 3.5f; v->idx = 0.22f; v->idec = dk(0.35f); v->adec = dk(1.2f); v->rel = dk(0.9f); v->snd = 0.45f; vel *= 0.6f; break;
    case K_BOX:   v->ratio = 4.f; v->idx = 0.12f; v->idec = dk(0.12f); v->adec = dk(0.9f); v->rel = dk(0.9f); v->snd = 0.5f; vel *= 0.7f; break;
    default:      v->ratio = 0; v->sq = true; v->idx = 0; v->idec = 1; v->adec = dk(0.16f); v->rel = dk(0.05f); v->snd = 0.15f; vel *= 1.0f; break;
  }
  v->amp = vel;
  float a = (pan + 1.f) * 0.78539816f; v->gl = cosf(a); v->gr = sinf(a);
}
static void chord_notes(const chord_t *c, int key, int lo, int *out, int *n) {
  int pcs[5] = {c->root, c->root + c->t, c->root + c->f, c->root + c->s, c->root + c->a}, k = 0;
  for (int i = 0; i < 5; i++) { if ((i == 3 && !c->s) || (i == 4 && !c->a)) continue; out[k++] = place(key + pcs[i], lo); }
  *n = k;
}
static int arp_note(const chord_t *c, int key, int lo, int d) {
  int ext[4] = {0, c->t, c->f, c->s}, n = c->s ? 4 : 3;
  return place(key + c->root, lo) + ext[d % n] + 12 * (d / n);
}

static void song_reset(void) {
  sq_bar = 0; sq_st = 0; sq_ctr = 0; gt_last = 'x'; pn_last = '.';
  memset(pv, 0, sizeof pv); memset(&bs, 0, sizeof bs); memset(&pad, 0, sizeof pad); memset(&ld, 0, sizeof ld);
  memset(&gt, 0, sizeof gt); memset(&dm, 0, sizeof dm);
  if (cur < 0 || cur >= KMOOD_SILENT) return;
  const song *S = &SONGS[cur];
  st_len = SR * 60.f / S->bpm / 4.f;
  pad_k = lpk(S->pad_cut); gtk_open = lpk(3400); gtk_mute = lpk(1500); gthp = lpk(110); ldk = lpk(5200);
  bsdrive = S->bass_drive;
}

static void seq_step(int si) {
  const song *S = &SONGS[si];
  int bar = sq_bar, st = sq_st, nb = NB[si], gs = bar * 16 + st;
  char sec = S->form[bar], nsec = S->form[bar + 1 < nb ? bar + 1 : S->loop];
  const part *Pp = part_of(S, sec);
  cur_fl = Pp->fl;
  int bis = BSEC[si][bar];
  const chord_t *c = &CH[si][bar * 2 + (st >= 8)];
  bool newch = st == 0 || (st == 8 && memcmp(&CH[si][bar * 2], &CH[si][bar * 2 + 1], sizeof(chord_t)));
  int key = S->key;

  /* drums, with a fill into a new section and a short one every eighth bar */
  if (Pp->drum) {
    const char *D = DRUMS[Pp->drum];
    char k = D[st], s = D[16 + st], h = D[32 + st], cy = D[48 + st];
    bool big = nsec != sec && st >= 12, small = nsec == sec && (bis & 7) == 7 && st >= 14;
    if (!(Pp->fl & F_NOFILL) && (big || small)) {
      h = '.'; cy = '.';
      if (big) { k = st == 12 ? 'x' : '.'; s = st < 14 ? 'x' : '.'; if (st >= 14) { dm.tenv = 1; dm.tf = st == 14 ? 150.f : 105.f; dm.tph = 0; } }
      else s = 'x';
    }
    if (k == 'x') { dm.kenv = 1; dm.kp = 1; dm.kclick = 1; }
    if (s == 'x' || s == 'g') { dm.senv = 1; dm.sv = s == 'x' ? 1.f : 0.3f; }
    if (h != '.') { dm.henv = 1; dm.hv = h == 'g' ? 0.45f : h == 'o' ? 0.8f : 0.75f; dm.hdec = h == 'o' ? hdec_o : hdec_c; }
    if (cy == 'r') { dm.renv = 1; }
    /* a crash on the one: each section start for the band kits, every four bars where the part asks */
    bool band = (0xBA37Eu >> Pp->drum) & 1;
    if (st == 0 && ((bis == 0 && band) || ((Pp->fl & F_CRASH) && (bis & 3) == 0))) dm.cenv = 1;
  }

  /* bass */
  {
    char b = BASS[Pp->bass][st];
    if (b == '-' && newch && st == 8) b = 'r';
    if (b == '.') bs.gate = 0;
    else if (b != '-') {
      int r = place(key + c->root, S->bass_lo), n = r;
      if (b == 'o') n = r + 12; else if (b == 'f') n = r + 7; else if (b == 't') n = r + c->t; else if (b == '7') n = r + (c->s ? c->s : 10);
      else if (b == 'k') n = place(key + BMOD[si][bar], S->bass_lo);
      else if (b == 'a') { const chord_t *nx = st < 8 ? &CH[si][bar * 2 + 1] : &CH[si][(bar + 1 < nb ? bar + 1 : S->loop) * 2]; n = place(key + nx->root, S->bass_lo) - 1; }
      bs.inc = mtof((float)n) / SR; bs.gate = 1; bs.env = 1;
    }
  }

  /* pad: follows the chord, re-bowing only the voices whose note moved */
  pad.gate = (Pp->fl & F_PAD) ? 1.f : 0.f;
  if (newch) {
    int ns[5], n; chord_notes(c, key, S->pad_lo, ns, &n); if (n > 4) n = 4;
    for (int i = 0; i < 4; i++) {
      float inc = i < n ? mtof((float)ns[i]) / SR : 0;
      if (fabsf(inc - pad.inc[i]) > 1e-7f) { pad.inc[i] = inc; if (inc > 0) pad.amp[i] *= 0.3f; }
      pad.on[i] = i < n ? 1.f : 0.f;
    }
  }

  /* piano comp */
  if (Pp->pno) {
    char p = COMP[Pp->pno][st];
    if (p == '-' && newch && (pn_last == 'x' || pn_last == 'p')) p = pn_last;
    if (p == '.') pv_off(1);
    else if (p == 'x') { int ns[5], n; pv_off(1); chord_notes(c, key, S->pno_lo, ns, &n); for (int i = 0; i < n; i++) pv_on(1, (float)ns[i], S->pno_k, 0.32f, 0.3f - 0.15f * i); }
    else if (p == 'p') { int r = place(key + c->root, S->pno_lo - 12); pv_off(1); pv_on(1, (float)r, S->pno_k, 0.5f, 0.1f); pv_on(1, (float)r + 12, S->pno_k, 0.4f, 0.2f); }
    else if (p >= '0' && p <= '9') pv_on(1, (float)arp_note(c, key, S->pno_lo, p - '0'), S->pno_k, 0.4f, 0.25f);
    if (p != '-') pn_last = p;
  } else if (st == 0) pv_off(1);
  /* arp on the bell or the pluck */
  if (Pp->arp) {
    char p = COMP[Pp->arp][st];
    if (p >= '0' && p <= '9') pv_on(3, (float)arp_note(c, key, S->pno_lo + 12, p - '0'), S->arp_k, 0.4f, (st & 2) ? -0.4f : 0.4f);
    else if (p == '.') pv_off(3);
  }

  /* guitar */
  {
    char g = Pp->gtr ? GTR[Pp->gtr][st] : '.';
    if (g == '-' && newch) g = gt_last;
    if (g == 'x' || g == 'm') {
      int r = place(key + c->root, S->gtr_lo);
      float n3[3] = {(float)r, (float)r + 7, (float)r + 12};
      for (int i = 0; i < 3; i++) gt.inc[i] = mtof(n3[i]) / SR;
      gt.mute = g == 'm'; gt.amp = g == 'm' ? 0.8f : 1.f; gt.dec = dk(g == 'm' ? 0.07f : 2.5f); gt.gate = 1; gt_last = g;
    } else if (g == '.') { gt.dec = dk(0.03f); gt.gate = 0; }
  }

  /* melody */
  int mo = MON[si][gs]; bool gate = MGATE[si][gs];
  if (mo != -128) {
    float n = (float)(key + mo);
    if (Pp->fl & F_LEAD) {
      int kind = (Pp->fl & F_LEADB) ? S->lead_b : S->lead_a;
      float f = mtof(n + S->lead_oct);
      if (ld.gate > 0 && ld.kind == kind && ld.amp > 0.05f) { ld.ft = f; ld.amp *= 0.55f; }
      else { ld.f = ld.ft = f; ld.kind = kind; }
      ld.vt = 0; ld.gate = 1;
    }
    if (Pp->fl & F_OCT) { pv_off(2); pv_on(2, n + S->pmel_oct, S->pno_k, 0.5f, -0.1f); pv_on(2, n + S->pmel_oct + 12, S->pno_k, 0.4f, -0.2f); }
    if (Pp->fl & F_PMEL) { pv_off(2); pv_on(2, n + S->pmel_oct, K_PIANO, 0.5f, -0.15f); }
    if (Pp->fl & F_BELL) pv_on(4, n + S->bell_oct, S->bell_k, 0.45f, -0.3f);
  }
  if (!gate || !(Pp->fl & F_LEAD)) ld.gate = 0;
  if (!gate) pv_off(2);
}

/* one sample of the band into L, R and the sfx echo send */
static void music_sample(int si, float *oL, float *oR, float *osd) {
  const song *S = &SONGS[si];
  if (sq_ctr <= 0) {
    seq_step(si);
    sq_ctr += st_len * ((sq_st & 1) ? 1.f - S->swing : 1.f + S->swing);
    if (++sq_st == 16) { sq_st = 0; if (++sq_bar >= NB[si]) sq_bar = S->loop; }
  }
  sq_ctr -= 1;
  float L = 0, R = 0, vs = 0, es = 0;

  /* bass: a band limited saw and a sine, through a two pole that opens on each note */
  if (bs.amp > 1e-4f || bs.gate > 0) {
    bs.amp += (bs.gate * 1.f - bs.amp) * (bs.gate > bs.amp ? 0.02f : 0.006f);
    bs.ph = adv(bs.ph, bs.inc);
    float x = bsaw(bs.ph, bs.inc) * 0.55f + fsin(bs.ph) * 0.75f;
    float k = lpk(S->bass_cut * (0.45f + 0.9f * bs.env));
    bs.z1 += (x - bs.z1) * k; bs.z2 += (bs.z1 - bs.z2) * k;
    float y = bs.z2;
    if (bsdrive > 0) y = sat(y * (1.f + bsdrive * 3.f)) * 0.85f;
    bs.env *= 0.9997f;
    y *= bs.amp * S->v_bass * 0.7f * STEM(1);
    L += y; R += y;
  }
  /* strings: four notes of two detuned saws, one per side */
  pad.lvl += (pad.gate - pad.lvl) * (pad.gate > pad.lvl ? 0.00012f : 0.00008f) * (48000.f / SR);
  if (pad.lvl > 1e-4f) {
    float a = 0, b = 0;
    for (int i = 0; i < 4; i++) {
      pad.amp[i] += (pad.on[i] - pad.amp[i]) * 0.0004f * (48000.f / SR);
      if (pad.amp[i] < 1e-4f) continue;
      pad.ph[i * 2] = adv(pad.ph[i * 2], pad.inc[i] * 0.996f);
      pad.ph[i * 2 + 1] = adv(pad.ph[i * 2 + 1], pad.inc[i] * 1.004f);
      a += (pad.ph[i * 2] * 2.f - 1.f) * pad.amp[i]; b += (pad.ph[i * 2 + 1] * 2.f - 1.f) * pad.amp[i];
    }
    pad.zl1 += (a - pad.zl1) * pad_k; pad.zl2 += (pad.zl1 - pad.zl2) * pad_k;
    pad.zr1 += (b - pad.zr1) * pad_k; pad.zr2 += (pad.zr1 - pad.zr2) * pad_k;
    float g = pad.lvl * S->v_pad * 0.16f * STEM(2);
    if (cur_fl & F_TREM) { pad.tph = adv(pad.tph, 6.5f / SR); g *= 0.6f + 0.4f * fsin(pad.tph); }
    float yl = pad.zl2 * g, yr = pad.zr2 * g;
    L += yl * 0.85f + yr * 0.15f; R += yr * 0.85f + yl * 0.15f; vs += (yl + yr) * 0.25f;
  }
  /* piano, Rhodes, bells and plucks */
  for (int i = 0; i < NPV; i++) {
    pvoice *v = &pv[i];
    if (!v->on) continue;
    v->ph = adv(v->ph, v->inc);
    float x;
    if (v->sq) x = bpulse(v->ph, v->inc, 0.25f);
    else { v->mph = adv(v->mph, v->inc * v->ratio); x = fsin(v->ph + v->idx * fsin(v->mph)); v->idx *= v->idec; }
    x *= v->amp;
    v->amp *= v->held ? v->adec : v->rel;
    if (v->amp < 2e-4f) { v->on = false; continue; }
    float g = v->grp == 4 ? S->v_bell : v->grp == 3 ? S->v_bell : S->v_pno;
    x *= g * 0.6f * STEM(4);
    L += x * v->gl; R += x * v->gr; vs += x * v->snd;
  }
  /* lead */
  if (ld.amp > 1e-4f || ld.gate > 0) {
    float atk = ld.kind == LD_FLUTE ? 0.0015f : 0.006f;
    ld.amp += (ld.gate - ld.amp) * (ld.gate > ld.amp ? atk : 0.0012f) * (48000.f / SR);
    ld.f += (ld.ft - ld.f) * 0.0025f * (48000.f / SR);
    ld.vt += 1.f / SR;
    float vd = ld.vt > 0.22f ? fminf(1.f, (ld.vt - 0.22f) * 3.f) : 0.f;
    float inc = ld.f * (1.f + 0.006f * vd * fsin(ld.vt * 5.5f)) / SR;
    float x = 0, xl = 0, xr = 0;
    switch (ld.kind) {
      case LD_SUPER: {
        ld.ph[0] = adv(ld.ph[0], inc * 0.993f); ld.ph[1] = adv(ld.ph[1], inc); ld.ph[2] = adv(ld.ph[2], inc * 1.007f);
        float s0 = bsaw(ld.ph[0], inc), s1 = bsaw(ld.ph[1], inc), s2 = bsaw(ld.ph[2], inc);
        xl = s0 * 0.5f + s1 * 0.35f; xr = s2 * 0.5f + s1 * 0.35f; break; }
      case LD_SQUARE:
        ld.ph[0] = adv(ld.ph[0], inc); ld.ph[1] = adv(ld.ph[1], inc * 1.003f);
        x = bpulse(ld.ph[0], inc, 0.25f) * 0.8f + bpulse(ld.ph[1], inc, 0.5f) * 0.4f; xl = xr = x; break;
      case LD_FLUTE:
        ld.ph[0] = adv(ld.ph[0], inc);
        x = fsin(ld.ph[0]) + 0.18f * fsin(ld.ph[0] * 2.f) + rnd() * 0.05f * ld.amp; xl = xr = x * 0.9f; break;
      case LD_GTR: {
        ld.ph[0] = adv(ld.ph[0], inc); ld.ph[1] = adv(ld.ph[1], inc * 1.005f); ld.ph[2] = adv(ld.ph[2], inc * 0.5f);
        x = sat((bsaw(ld.ph[0], inc) + bsaw(ld.ph[1], inc) + 0.5f * bsaw(ld.ph[2], inc * 0.5f)) * 2.2f);
        ld.z2 += (x - ld.z2) * 0.35f; ld.z3 += (ld.z2 - ld.z3) * 0.35f; x = ld.z3 * 0.7f; xl = xr = x; break; }
      default:
        ld.ph[0] = adv(ld.ph[0], inc);
        x = (ld.ph[0] < 0.5f ? ld.ph[0] * 4.f - 1.f : 3.f - ld.ph[0] * 4.f) * 0.8f + fsin(ld.ph[0]) * 0.4f; xl = xr = x; break;
    }
    float g = ld.amp * S->v_lead * 0.9f * STEM(8);
    ld.z1 += (xl - ld.z1) * ldk; ld.zr += (xr - ld.zr) * ldk;
    xl = ld.z1 * g; xr = ld.zr * g;
    L += xl; R += xr; vs += (xl + xr) * 0.15f; es += (xl + xr) * 0.12f;
  }
  /* guitar: a power chord of saw pairs, one side each, pushed through a soft clip and a dark cab */
  if (gt.amp > 1e-4f) {
    float a = 0, b = 0;
    for (int i = 0; i < 3; i++) {
      gt.ph[i * 2] = adv(gt.ph[i * 2], gt.inc[i]); gt.ph[i * 2 + 1] = adv(gt.ph[i * 2 + 1], gt.inc[i] * 1.004f);
      a += bsaw(gt.ph[i * 2], gt.inc[i]); b += bsaw(gt.ph[i * 2 + 1], gt.inc[i]);
    }
    float k = gt.mute ? gtk_mute : gtk_open;
    a = sat(a * 1.6f); b = sat(b * 1.6f);
    gt.zl1 += (a - gt.zl1) * k; gt.zl2 += (gt.zl1 - gt.zl2) * k; gt.zr1 += (b - gt.zr1) * k; gt.zr2 += (gt.zr1 - gt.zr2) * k;
    gt.hl += (gt.zl2 - gt.hl) * gthp; gt.hr += (gt.zr2 - gt.hr) * gthp;
    float g = gt.amp * S->v_gtr * 0.5f * STEM(16);
    L += (gt.zl2 - gt.hl) * g; R += (gt.zr2 - gt.hr) * g;
    gt.amp *= gt.dec;
  }
  /* drums */
  float d = 0, dl = 0, dr = 0;
  if (dm.kenv > 1e-4f) {
    dm.kp *= kpdec;
    dm.kph = adv(dm.kph, (54.f + 150.f * dm.kp) / SR);
    d += sat((fsin(dm.kph) * dm.kenv + rnd() * dm.kclick * 0.3f) * 1.5f) * 0.5f;
    dm.kenv *= kdec; dm.kclick *= 0.92f;
  }
  if (dm.senv > 1e-4f) {
    dm.sph = adv(dm.sph, 190.f / SR);
    float n = rnd(); dm.sz += (n - dm.sz) * 0.3f;
    float y = (fsin(dm.sph) * 0.5f * dm.senv + (n - dm.sz) * 0.8f) * dm.senv * dm.sv * 0.55f;
    d += y; vs += y * 0.3f;
    dm.senv *= sdec;
  }
  if (dm.tenv > 1e-4f) {
    dm.tph = adv(dm.tph, dm.tf * (1.f + 0.5f * dm.tenv * dm.tenv) / SR);
    float y = fsin(dm.tph) * dm.tenv * 0.6f; d += y; vs += y * 0.2f; dm.tenv *= tdec;
  }
  if (dm.henv > 1e-4f) {
    float n = rnd(); dm.hz += (n - dm.hz) * 0.55f;
    float y = (n - dm.hz) * dm.henv * dm.hv * 0.22f; dl += y * 0.4f; dr += y * 0.6f;
    dm.henv *= dm.hdec;
  }
  if (dm.renv > 1e-4f) {
    dm.rph[0] = adv(dm.rph[0], 587.f / SR); dm.rph[1] = adv(dm.rph[1], 845.f / SR); dm.rph[2] = adv(dm.rph[2], 1243.f / SR);
    float m = (dm.rph[0] < 0.5f ? 1.f : -1.f) + (dm.rph[1] < 0.5f ? 1.f : -1.f) + (dm.rph[2] < 0.5f ? 1.f : -1.f) + rnd() * 0.8f;
    dm.rz += (m - dm.rz) * 0.5f;
    float y = (m - dm.rz) * dm.renv * 0.05f; dl += y * 0.65f; dr += y * 0.35f;
    dm.renv *= rdec;
  }
  if (dm.cenv > 1e-4f) {
    float n = rnd(); dm.cz += (n - dm.cz) * 0.35f; dm.cz2 += ((n - dm.cz) - dm.cz2) * 0.6f;
    float y = dm.cz2 * dm.cenv * 0.3f; dl += y; dr += y * 0.9f; vs += y * 0.25f;
    dm.cenv *= cdec;
  }
  float dv = S->v_drum * STEM(32);
  L += (d + dl) * dv; R += (d + dr) * dv;

  /* reverb */
  {
    float x = vs * S->verb * 0.7f + 1e-18f, o = 0;   /* the offset keeps decaying tails out of denormals */
    for (int i = 0; i < 4; i++) {
      float y = rc[i][rci[i]];
      rcz[i] = y * 0.7f + rcz[i] * 0.3f;
      rc[i][rci[i]] = x + rcz[i] * 0.8f;
      if (++rci[i] >= rcl[i]) rci[i] = 0;
      o += y;
    }
    float yl = o, yr = o;
    for (int i = 0; i < 2; i++) { float b = ra[i][rai[i]]; ra[i][rai[i]] = yl + b * 0.5f; yl = b - yl; if (++rai[i] >= ral[i]) rai[i] = 0; }
    for (int i = 2; i < 4; i++) { float b = ra[i][rai[i]]; ra[i][rai[i]] = yr + b * 0.5f; yr = b - yr; if (++rai[i] >= ral[i]) rai[i] = 0; }
    L += yl * STEM(64); R += yr * STEM(64);
  }
  *oL = L * S->gain; *oR = R * S->gain; *osd = es * S->gain;
}

static void music_init(void) {
  for (int i = 0; i <= 2048; i++) sint[i] = sinf((float)i / 2048.f * TAU);
  static const int cl[4] = {1116, 1188, 1277, 1356}, al[4] = {556, 441, 579, 464};
  for (int i = 0; i < 4; i++) {
    rcl[i] = (int)(cl[i] * SR / 44100.f); if (rcl[i] > RVC) rcl[i] = RVC;
    ral[i] = (int)(al[i] * SR / 44100.f); if (ral[i] > 1200) ral[i] = 1200;
    rci[i] = rai[i] = 0; rcz[i] = 0;
  }
  memset(rc, 0, sizeof rc); memset(ra, 0, sizeof ra);
  kdec = dk(0.16f); kpdec = dk(0.028f); sdec = dk(0.075f); hdec_c = dk(0.028f); hdec_o = dk(0.16f);
  rdec = dk(0.35f); cdec = dk(0.9f); tdec = dk(0.16f);
  song_err[0] = 0;
  for (int i = 0; i < KMOOD_SILENT; i++) song_build(i);
}

/* api */
static float sfx_vol = 0.9f, mus_vol = 0.5f;

void kaudio_init(uint32_t rate) {
  SR = rate ? (float)rate : 48000.f;
  memset(vox, 0, sizeof vox); memset(eb, 0, sizeof eb);
  cur = -1; want = KMOOD_SILENT; mgain = 0; mtarget = 1; ew = 0;
  music_init();
}

void kmusic_mood(kmood_t m) {
  want = (int)m;
}
int kmusic_playing(void) { return want != KMOOD_SILENT || mgain > 0.001f; }

/* continuous voice
 * The staff's charge is called every frame while it lasts, so it is not a pool
 * voice that stacks: each call refreshes its gain and time to live, and the
 * render loop runs one oscillator set, fading in and out around the calls. */
typedef struct { float ttl, gain, pan, level, lpan, ph[3], z[3], t; } cont;
static cont coil_c;

static cont *cont_slot(cont *arr, int n, float pan) {
  cont *free_ = NULL, *weak = &arr[0];
  for (int i = 0; i < n; i++) {
    if (arr[i].ttl > 0 && fabsf(arr[i].pan - pan) < 0.3f) return &arr[i];
    if (arr[i].ttl <= 0 && !free_) free_ = &arr[i];
    if (arr[i].level < weak->level) weak = &arr[i];
  }
  return free_ ? free_ : weak;
}
static void cont_call(cont *arr, int n, float pan, float gain) {
  cont *c = cont_slot(arr, n, pan);
  if (c->ttl <= 0) c->lpan = pan;
  c->ttl = 0.09f; c->gain = gain; c->pan = pan;
}

/* the staff charging: a capacitor whine climbing with the charge, a mains hum under it, crackle near the top */
static float coil_sample(cont *c, float dt, float sd_[1]) {
  float k = c->gain;   /* the charge itself: call gain is 0..1 */
  float f = 180.f + 2700.f * powf(k, 1.6f);
  c->ph[0] = sgn_frac(c->ph[0] + f * dt);
  c->ph[1] = sgn_frac(c->ph[1] + f * 1.5f * dt);
  c->ph[2] = sgn_frac(c->ph[2] + 100.f * dt);
  float w = sinf(c->ph[0] * TAU + (1.2f + 2.f * k) * sinf(c->ph[1] * TAU));
  c->z[0] += (osc(W_SAW, c->ph[2], 0) - c->z[0]) * fminf(1.f, 260.f * TAU * dt);
  float x = w * 0.5f + c->z[0] * 0.5f;
  if (k > 0.5f && rnd() > 1.f - 0.003f * k * k * k) c->z[1] = 1.f;   /* a spark */
  c->z[1] *= 1.f - 220.f * dt;
  x += c->z[1] * rnd() * 0.8f;
  float y = x * c->level * (0.085f + 0.31f * k);
  sd_[0] += y * 0.15f;
  return y;
}

/* effects */
/* loudness trims, measured: the loudest 100 ms of each effect against the music (about -17 dB RMS).
 * Swings, the foe's glint and the UI sit near -16; hits and breaks near -10; KO and the special
 * near -5. 0 means as written. CHARGE and STYLE use the gain as a parameter; their levels are in their code. */
static const float KSFX_TRIM[KSFX_COUNT] = {
  [KSFX_SWING_ARC] = 2.8f, [KSFX_SWING_MAUL] = 1.8f, [KSFX_SWING_TWIN] = 4.f, [KSFX_SWING_SPEAR] = 3.5f,
  [KSFX_BOLT] = 2.f, [KSFX_GLINT] = 2.5f, [KSFX_HIT_LIGHT] = 1.3f, [KSFX_CLASH] = 2.f,
  [KSFX_PARRY] = 1.6f, [KSFX_ROAR] = 1.8f, [KSFX_DODGE] = 2.f, [KSFX_SUMMON] = 2.f, [KSFX_JUMP] = 2.5f,
  [KSFX_LAND] = 0.67f, [KSFX_PICKUP] = 1.5f, [KSFX_WEAPON] = 2.5f, [KSFX_UI] = 2.5f, [KSFX_UI_OK] = 1.4f,
  [KSFX_TELL] = 1.8f, [KSFX_ASSIST] = 1.8f,
};
void ksfx(ksfx_t s, float pan, float gain) {
  if (s < 0 || s >= KSFX_COUNT) return;
  float g = gain <= 0 ? 0 : gain > 1 ? 1 : gain, p = pan;
  if (KSFX_TRIM[s] > 0) g *= KSFX_TRIM[s];
  voice *v;
  float j = 1.f + rnd() * 0.08f;   /* a little pitch jitter so repeats do not machine gun */
#define V(w, f0, f1, d, vol) vx((int)s, w, f0, f1, d, (vol) * g, p)
  switch (s) {
    case KSFX_CHARGE: cont_call(&coil_c, 1, pan, g); break;
    case KSFX_SWING_ARC: {   /* plasma: a hiss with a buzz inside it, rising then gone */
      v = V(W_NOISE, 0, 0, 0.2f * 1.f, 0.7f); v->lp = 1800; v->lp1 = 7000; v->hp = 700; v->atk = 0.03f; v->curve = 2;
      v = V(W_SAW, 150 * j, 420 * j, 0.16f, 0.22f); v->lp = 2400; v->curve = 2; v->atk = 0.02f;
      v = V(W_SINE, 900 * j, 1900 * j, 0.14f, 0.12f); v->fm_r = 3.1f; v->fm_i = 3.f; v->curve = 2; v->send = 0.15f;
      break; }
    case KSFX_SWING_MAUL: {   /* a great weight moves air */
      v = V(W_NOISE, 0, 0, 0.42f, 0.95f); v->lp = 240; v->lp1 = 1300; v->atk = 0.14f; v->curve = 2;
      v = V(W_SINE, 70 * j, 130 * j, 0.36f, 0.6f); v->atk = 0.12f; v->curve = 2;
      v = V(W_TRI, 210, 340, 0.2f, 0.14f); v->delay = 0.1f; v->curve = 3;
      break; }
    case KSFX_SWING_TWIN: {   /* a quick thin whip */
      v = V(W_NOISE, 0, 0, 0.11f, 0.6f); v->lp = 3000; v->lp1 = 9500; v->hp = 1800; v->atk = 0.02f; v->curve = 2;
      v = V(W_SINE, 2300 * j, 3400 * j, 0.06f, 0.1f); v->curve = 2;
      break; }
    case KSFX_SWING_SPEAR: {   /* a long airy sweep */
      v = V(W_NOISE, 0, 0, 0.3f, 0.85f); v->lp = 500; v->lp1 = 3600; v->hp = 240; v->atk = 0.1f; v->curve = 2;
      v = V(W_TRI, 180 * j, 420 * j, 0.24f, 0.2f); v->atk = 0.08f; v->curve = 2;
      break; }
    case KSFX_BOLT: {   /* a laser cracking off */
      v = V(W_SQ, 1500 * j, 240, 0.14f, 0.4f); v->duty = 0.3f; v->lp = 5200; v->curve = 2; v->send = 0.25f;
      v = V(W_SINE, 2400 * j, 700, 0.1f, 0.3f); v->fm_r = 2.f; v->fm_i = 2.f; v->curve = 2;
      break; }
    case KSFX_LANCE: {   /* a crack, a chest thump, a tearing beam, a ringing tail */
      v = V(W_NOISE, 0, 0, 0.09f, 1.1f); v->hp = 1800; v->curve = 2;
      v = V(W_SINE, 130, 34, 0.4f, 1.1f); v->drive = 1.4f; v->curve = 2;
      v = V(W_SAW, 1100, 180, 0.5f, 0.32f); v->lp = 4200; v->lp1 = 800; v->curve = 2; v->send = 0.4f;
      v = V(W_SINE, 1760, 1720, 0.8f, 0.14f); v->fm_r = 1.41f; v->fm_i = 2.f; v->curve = 3; v->send = 0.5f;
      break; }
    case KSFX_GLINT: {   /* a small bright ting */
      v = V(W_SINE, 3700 * j, 3650 * j, 0.32f, 0.26f); v->fm_r = 2.76f; v->fm_i = 1.2f; v->curve = 3; v->send = 0.4f;
      v = V(W_SINE, 5200 * j, 5100 * j, 0.14f, 0.1f); v->curve = 2; v->delay = 0.02f;
      break; }
    case KSFX_HIT_LIGHT: {   /* a slap and a thud, with a slice on top */
      v = V(W_NOISE, 0, 0, 0.06f, 0.95f); v->lp = 7800; v->lp1 = 1500; v->curve = 2;
      v = V(W_SINE, 240 * j, 70, 0.13f, 1.0f); v->drive = 1.3f; v->curve = 2;
      v = V(W_SQ, 780 * j, 300, 0.05f, 0.18f); v->lp = 3200; v->curve = 2;
      break; }
    case KSFX_HIT_HEAVY: {   /* a body blow you feel */
      v = V(W_SINE, 170 * j, 38, 0.32f, 1.25f); v->drive = 2.2f; v->curve = 2;
      v = V(W_NOISE, 0, 0, 0.13f, 1.05f); v->lp = 5200; v->lp1 = 480; v->curve = 2; v->send = 0.15f;
      v = V(W_SQ, 460 * j, 110, 0.1f, 0.3f); v->lp = 1800; v->curve = 2;
      break; }
    case KSFX_CLASH: {   /* steel on steel: inharmonic rings and a chip of noise */
      v = V(W_NOISE, 0, 0, 0.04f, 0.8f); v->hp = 2600; v->curve = 2;
      v = V(W_SINE, 1560 * j, 1520 * j, 0.3f, 0.36f); v->fm_r = 1.47f; v->fm_i = 3.2f; v->curve = 3; v->send = 0.28f;
      v = V(W_SINE, 2630 * j, 2590 * j, 0.2f, 0.22f); v->fm_r = 2.31f; v->fm_i = 2.f; v->curve = 3;
      break; }
    case KSFX_PARRY: {   /* a struck bell with a rising sparkle */
      v = V(W_NOISE, 0, 0, 0.05f, 0.9f); v->hp = 2000; v->curve = 2;
      v = V(W_SINE, 1180, 1170, 0.9f, 0.5f); v->fm_r = 2.41f; v->fm_i = 3.8f; v->curve = 3; v->send = 0.55f;
      v = V(W_SINE, 2360, 2340, 0.6f, 0.26f); v->fm_r = 1.47f; v->fm_i = 2.2f; v->curve = 3; v->send = 0.45f;
      v = V(W_TRI, 1320, 2640, 0.18f, 0.2f); v->delay = 0.05f; v->curve = 2; v->send = 0.4f;
      break; }
    case KSFX_GUARD_BREAK: {   /* a guard shatters */
      v = V(W_NOISE, 0, 0, 0.2f, 1.1f); v->lp = 7200; v->lp1 = 600; v->curve = 2;
      v = V(W_SINE, 300, 48, 0.36f, 1.1f); v->drive = 1.8f; v->curve = 2;
      for (int i = 0; i < 3; i++) { v = V(W_SINE, 1900.f - i * 370.f, 1500.f - i * 300.f, 0.22f, 0.2f); v->delay = 0.03f + i * 0.045f; v->fm_r = 1.7f; v->fm_i = 2.f; v->curve = 3; v->send = 0.25f; }
      break; }
    case KSFX_BODY_FALL: {   /* a body meets the floor, then a smaller bounce */
      v = V(W_SINE, 120 * j, 42, 0.24f, 1.1f); v->drive = 1.4f; v->curve = 2;
      v = V(W_NOISE, 0, 0, 0.14f, 0.55f); v->lp = 1400; v->lp1 = 260; v->curve = 2;
      v = V(W_SINE, 95, 40, 0.14f, 0.5f); v->delay = 0.17f; v->curve = 2;
      break; }
    case KSFX_SHOCK: {   /* a slam that lifts the floor */
      v = V(W_SINE, 95, 28, 0.6f, 1.4f); v->drive = 2.4f; v->curve = 2;
      v = V(W_NOISE, 0, 0, 0.4f, 1.0f); v->lp = 3600; v->lp1 = 180; v->curve = 2; v->send = 0.3f;
      v = V(W_SQ, 210, 55, 0.2f, 0.3f); v->lp = 1300; v->curve = 2;
      break; }
    case KSFX_ROAR: {   /* a rising low growl and a wall of air */
      v = V(W_SAW, 62, 190, 0.8f, 0.6f); v->lp = 400; v->lp1 = 2400; v->atk = 0.2f; v->curve = 2; v->drive = 1.6f; v->wob = 38; v->send = 0.25f;
      v = V(W_NOISE, 0, 0, 0.75f, 0.7f); v->lp = 500; v->lp1 = 3600; v->atk = 0.25f; v->curve = 2;
      break; }
    case KSFX_DODGE: {
      v = V(W_NOISE, 0, 0, 0.26f, 0.8f); v->lp = 500; v->lp1 = 3000; v->hp = 200; v->atk = 0.06f; v->curve = 2;
      v = V(W_TRI, 170, 90, 0.16f, 0.35f); v->curve = 2;
      break; }
    case KSFX_PERFECT: {   /* the world drops away: a falling tone, a clear chime, a long echo */
      v = V(W_SINE, 900, 170, 0.5f, 0.45f); v->curve = 2; v->send = 0.4f;
      v = V(W_SINE, 1760, 1755, 0.8f, 0.36f); v->fm_r = 2.f; v->fm_i = 1.2f; v->curve = 3; v->send = 0.7f; v->delay = 0.04f;
      v = V(W_SINE, 2637, 2630, 0.65f, 0.22f); v->curve = 3; v->send = 0.7f; v->delay = 0.1f;
      break; }
    case KSFX_SUMMON: {   /* a blade of light drawn from nothing */
      v = V(W_SAW, 200, 1700, 0.22f, 0.3f); v->lp = 900; v->lp1 = 6000; v->atk = 0.08f; v->curve = 1; v->send = 0.3f;
      v = V(W_NOISE, 0, 0, 0.28f, 0.45f); v->lp = 1200; v->lp1 = 7600; v->hp = 600; v->atk = 0.1f; v->curve = 2;
      v = V(W_SINE, 2093, 2093, 0.5f, 0.25f); v->delay = 0.18f; v->fm_r = 2.f; v->fm_i = 1.4f; v->curve = 3; v->send = 0.5f;
      break; }
    case KSFX_SPECIAL_CHARGE: {   /* a half second climb to the break point, the world holding its breath */
      v = V(W_SAW, 80, 1300, 0.52f, 0.5f); v->lp = 400; v->lp1 = 6800; v->atk = 0.02f; v->curve = 1; v->send = 0.35f; v->drive = 0.6f;
      v = V(W_NOISE, 0, 0, 0.52f, 0.5f); v->lp = 500; v->lp1 = 9000; v->hp = 800; v->atk = 0.4f; v->curve = 1;
      v = V(W_SINE, 55, 110, 0.5f, 0.7f); v->curve = 1;
      break; }
    case KSFX_SPECIAL_RELEASE: {   /* everything at once */
      v = V(W_SINE, 110, 24, 1.2f, 1.6f); v->drive = 3.f; v->curve = 2;
      v = V(W_NOISE, 0, 0, 0.9f, 1.3f); v->lp = 9000; v->lp1 = 200; v->curve = 2; v->send = 0.5f;
      v = V(W_SAW, 880, 110, 0.7f, 0.4f); v->lp = 5200; v->lp1 = 500; v->curve = 2; v->send = 0.5f;
      v = V(W_SINE, 1318, 1310, 1.4f, 0.28f); v->fm_r = 1.41f; v->fm_i = 3.f; v->curve = 3; v->send = 0.8f;
      break; }
    case KSFX_KO: {   /* the last blow: a low bell, cut short by silence */
      v = V(W_SINE, 58, 34, 1.1f, 1.3f); v->drive = 2.f; v->curve = 2;
      v = V(W_SINE, 620, 610, 1.3f, 0.36f); v->fm_r = 1.41f; v->fm_i = 4.2f; v->curve = 3; v->send = 0.9f;
      v = V(W_NOISE, 0, 0, 0.14f, 0.9f); v->hp = 1500; v->curve = 2;
      break; }
    case KSFX_JUMP: {
      v = V(W_NOISE, 0, 0, 0.16f, 0.5f); v->lp = 700; v->lp1 = 3000; v->hp = 300; v->atk = 0.03f; v->curve = 2;
      v = V(W_TRI, 130, 300, 0.12f, 0.24f); v->curve = 2;
      break; }
    case KSFX_LAND: {
      v = V(W_SINE, 110, 36, 0.24f, 0.95f); v->curve = 2;
      v = V(W_NOISE, 0, 0, 0.12f, 0.45f); v->lp = 1200; v->lp1 = 200; v->curve = 3;
      break; }
    case KSFX_STEP: {
      v = V(W_NOISE, 0, 0, 0.05f, 0.5f); v->lp = 1200 * j; v->lp1 = 300; v->curve = 2;
      v = V(W_SINE, 110 * j, 60, 0.05f, 0.5f); v->curve = 2;
      break; }
    case KSFX_PICKUP: {
      v = V(W_TRI, 880, 880, 0.08f, 0.4f); v->curve = 1;
      v = V(W_TRI, 1320, 1320, 0.14f, 0.4f); v->delay = 0.07f; v->curve = 2; v->send = 0.3f;
      break; }
    case KSFX_WEAPON: {   /* a change of weapon: a quick rise and a latch */
      v = V(W_NOISE, 0, 0, 0.14f, 0.5f); v->lp = 1200; v->lp1 = 6000; v->hp = 600; v->atk = 0.05f; v->curve = 2;
      v = V(W_SQ, 880, 1760, 0.08f, 0.14f); v->duty = 0.3f; v->lp = 4000; v->curve = 1;
      v = V(W_NOISE, 0, 0, 0.03f, 0.6f); v->hp = 1400; v->delay = 0.1f; v->curve = 2;
      break; }
    case KSFX_UI: {
      v = V(W_SQ, 1900, 1500, 0.03f, 0.4f); v->duty = 0.3f; v->curve = 1; v->lp = 5000;
      break; }
    case KSFX_UI_OK: {
      v = V(W_TRI, 660, 660, 0.09f, 0.4f); v->curve = 1;
      v = V(W_TRI, 990, 990, 0.16f, 0.4f); v->delay = 0.07f; v->curve = 2; v->send = 0.25f;
      break; }
    case KSFX_SMASH: {   /* a hard cut: a body hit through a big room, a bright ring on top */
      v = V(W_SINE, 140, 30, 0.5f, 1.4f); v->drive = 2.6f; v->curve = 2;
      v = V(W_NOISE, 0, 0, 0.18f, 1.1f); v->lp = 9000; v->lp1 = 700; v->curve = 2; v->send = 0.5f;
      v = V(W_SINE, 1480, 1470, 0.9f, 0.3f); v->fm_r = 1.41f; v->fm_i = 2.6f; v->curve = 3; v->send = 0.7f;
      break; }
    case KSFX_TELL: {   /* a low swell that tightens, then a sting */
      v = V(W_SAW, 55, 110, 0.55f, 0.55f); v->lp = 300; v->lp1 = 1800; v->atk = 0.4f; v->curve = 1; v->drive = 1.2f;
      v = V(W_SINE, 2960, 2950, 0.5f, 0.3f); v->delay = 0.42f; v->fm_r = 2.76f; v->fm_i = 1.6f; v->curve = 3; v->send = 0.6f;
      v = V(W_NOISE, 0, 0, 0.08f, 0.6f); v->delay = 0.42f; v->hp = 2400; v->curve = 2;
      break; }
    case KSFX_TRAIN: {   /* the bell three times, then a two note horn */
      for (int i = 0; i < 3; i++) { v = V(W_SINE, 1250, 1245, 0.35f, 0.4f); v->delay = i * 0.32f; v->fm_r = 2.4f; v->fm_i = 2.2f; v->curve = 3; v->send = 0.3f; }
      v = V(W_SAW, 311, 311, 0.9f, 0.32f); v->delay = 1.0f; v->lp = 1400; v->atk = 0.05f; v->curve = 1; v->send = 0.3f;
      v = V(W_SAW, 392, 392, 0.9f, 0.28f); v->delay = 1.0f; v->lp = 1400; v->atk = 0.05f; v->curve = 1; v->send = 0.3f;
      break; }
    case KSFX_HISS: {
      v = V(W_NOISE, 0, 0, 0.9f, 0.5f); v->hp = 2600; v->lp = 9000; v->lp1 = 5000; v->atk = 0.12f; v->curve = 2;
      break; }
    case KSFX_SLAM: {
      v = V(W_SINE, 110, 40, 0.3f, 1.3f); v->drive = 2.f; v->curve = 2;
      v = V(W_NOISE, 0, 0, 0.12f, 1.f); v->lp = 4200; v->lp1 = 400; v->curve = 2; v->send = 0.25f;
      v = V(W_SQ, 330, 300, 0.22f, 0.12f); v->delay = 0.05f; v->wob = 30; v->lp = 2000; v->curve = 2;
      break; }
    case KSFX_ASSIST: {   /* a rising call and a bright answer */
      v = V(W_SAW, 220, 880, 0.3f, 0.3f); v->lp = 900; v->lp1 = 5000; v->atk = 0.05f; v->curve = 1; v->send = 0.4f;
      v = V(W_TRI, 1320, 1320, 0.3f, 0.35f); v->delay = 0.22f; v->curve = 2; v->send = 0.5f;
      v = V(W_TRI, 1760, 1760, 0.4f, 0.3f); v->delay = 0.3f; v->curve = 3; v->send = 0.6f;
      break; }
    case KSFX_STYLE: {   /* the gain sets the pitch, so a climbing rank climbs */
      float f = 700.f + 900.f * g;
      v = vx((int)s, W_TRI, f, f, 0.12f, 0.7f, p); v->curve = 2; v->send = 0.3f;
      v = vx((int)s, W_SINE, f * 1.5f, f * 1.5f, 0.2f, 0.4f, p); v->delay = 0.05f; v->curve = 3; v->send = 0.4f;
      break; }
    case KSFX_QTE: {   /* the world drops out: a long falling tone and a heartbeat */
      v = V(W_SINE, 600, 90, 1.0f, 0.4f); v->curve = 2; v->send = 0.6f;
      v = V(W_SINE, 70, 50, 0.16f, 1.f); v->drive = 1.5f; v->curve = 2;
      v = V(W_SINE, 70, 50, 0.16f, 0.8f); v->delay = 0.28f; v->drive = 1.5f; v->curve = 2;
      break; }
    case KSFX_TRAIN_BELL: {   /* a light rail gong: two strikes, inharmonic partials, a long ring */
      for (int i = 0; i < 2; i++) {
        float d = i * 0.3f, k = i ? 0.85f : 1.f;
        v = V(W_SINE, 1046, 1040, 1.1f, 0.42f * k); v->fm_r = 2.76f; v->fm_i = 1.6f; v->curve = 3; v->delay = d; v->send = 0.35f;
        v = V(W_SINE, 2520, 2510, 0.6f, 0.16f * k); v->fm_r = 1.41f; v->fm_i = 0.8f; v->curve = 3; v->delay = d + 0.002f;
        v = V(W_SINE, 523, 521, 1.3f, 0.22f * k); v->curve = 2; v->delay = d;
        v = V(W_NOISE, 0, 0, 0.02f, 0.35f * k); v->hp = 2500; v->curve = 2; v->delay = d;
      }
      break; }
    case KSFX_BIKE_BELL: {   /* a thumb bell: brass partials, struck twice */
      for (int i = 0; i < 2; i++) {
        v = V(W_SINE, 2960 * j, 2950 * j, 0.5f, 0.3f); v->fm_r = 2.76f; v->fm_i = 1.1f; v->curve = 3; v->delay = i * 0.16f; v->send = 0.25f;
        v = V(W_SINE, 4430 * j, 4420 * j, 0.25f, 0.12f); v->curve = 3; v->delay = i * 0.16f;
        v = V(W_NOISE, 0, 0, 0.012f, 0.3f); v->hp = 4000; v->curve = 2; v->delay = i * 0.16f;
      }
      break; }
    case KSFX_BIKE_TICK: {   /* one pawl click, small and dry */
      v = V(W_NOISE, 0, 0, 0.012f, 0.5f); v->hp = 3500; v->lp = 9000; v->curve = 2;
      v = V(W_SQ, 2600 * j, 2400, 0.008f, 0.08f); v->duty = 0.2f; v->curve = 2;
      break; }
    case KSFX_SHEATHE: {   /* the blade folding away: a falling shimmer and a soft latch */
      v = V(W_SINE, 1800 * j, 600, 0.22f, 0.18f); v->fm_r = 1.5f; v->fm_i = 1.5f; v->curve = 2; v->send = 0.3f;
      v = V(W_NOISE, 0, 0, 0.16f, 0.35f); v->lp = 6000; v->lp1 = 1200; v->hp = 500; v->curve = 2;
      v = V(W_NOISE, 0, 0, 0.03f, 0.4f); v->hp = 1400; v->delay = 0.16f; v->curve = 2;
      break; }
    case KSFX_ALARM: {
      for (int i = 0; i < 4; i++) { v = V(W_SQ, i & 1 ? 660 : 880, i & 1 ? 660 : 880, 0.17f, 0.6f); v->delay = i * 0.18f; v->duty = 0.4f; v->lp = 3200; v->curve = 1; v->atk = 0.006f; v->send = 0.2f; }
      break; }
    default: break;
  }
#undef V
}

void kaudio_mix(float *outl, float *outr, int nn, float gain) {
  uint32_t n = (uint32_t)nn;
  float dt = 1.f / SR;
  int e1 = (int)(0.19f * SR), e2 = (int)(0.253f * SR);
  if (e1 >= ECH) e1 = ECH - 1; if (e2 >= ECH) e2 = ECH - 1;
  for (uint32_t i = 0; i < n; i++) {
    /* mood changes fade the old tune down, then start the new one from its top */
    float mtgt = (want == cur && cur >= 0 && cur != KMOOD_SILENT) ? mtarget : 0.f;
    if (want == KMOOD_SILENT) mtgt = 0.f;
    mgain += (mtgt * music_on - mgain) * (mtgt > mgain ? 0.0004f : 0.0006f) * (48000.f / SR);
    if (want != cur && mgain < 0.01f) { cur = want; mtarget = 1; song_reset(); mgain = 0; }
    float mL = 0, mR = 0, msd = 0;
    if (cur >= 0 && cur < KMOOD_SILENT && (mgain > 0.0005f || mtgt > 0)) {
      music_sample(cur, &mL, &mR, &msd);
      mL *= mgain * mus_vol; mR *= mgain * mus_vol; msd *= mgain * mus_vol;
    }
    float L = mL, R = mR, sd = msd;
    for (int k = 0; k < NV; k++) {
      voice *v = &vox[k];
      if (!v->on) continue;
      if (v->delay > 0) { v->delay -= dt; continue; }
      float u = 1.f - v->age / v->dur;
      if (u <= 0) { v->on = false; continue; }
      float env = u; for (int c = 1; c < v->curve; c++) env *= u;
      if (v->age < v->atk) env *= v->age / v->atk;
      v->f *= v->mult;
      float f = v->f;
      if (v->wob > 0) f *= 1.f + 0.12f * sinf(v->age * v->wob);
      float x;
      if (v->wave == W_NOISE) x = rnd();
      else {
        v->ph = sgn_frac(v->ph + f * dt);
        if (v->fm_i > 0) {
          v->mph = sgn_frac(v->mph + f * v->fm_r * dt);
          x = sinf(v->ph * TAU + v->fm_i * env * sinf(v->mph * TAU));
        } else x = osc(v->wave, v->ph, v->duty);
      }
      if (v->lp > 0) {
        float fc = v->lp1 > 0 ? v->lp + (v->lp1 - v->lp) * (v->age / v->dur) : v->lp;
        v->lz += (x - v->lz) * fminf(1.f, fc * TAU * dt);
        x = v->lz;
      }
      if (v->hp > 0) { v->hz += (x - v->hz) * fminf(1.f, v->hp * TAU * dt); x -= v->hz; }
      if (v->drive > 0) x = tanhf(x * (1.f + v->drive));
      x *= env * v->vol;
      L += x * v->gl; R += x * v->gr; sd += x * v->send;
      v->age += dt;
    }
    /* continuous voices: refreshed by calls, they fade in and out around them */
    {
      float sdv[1] = {0};
      cont *cc = &coil_c;
      cc->ttl -= dt;
      float tg = cc->ttl > 0 ? 1.f : 0.f;
      cc->level += (tg - cc->level) * (tg > cc->level ? 0.002f : 0.004f) * (48000.f / SR);
      if (cc->level > 0.0005f) {
        float y = coil_sample(cc, dt, sdv);
        L += y; R += y;
      } else cc->level = 0;
      sd += sdv[0];
    }
    /* echo bus, a touch darker on each repeat */
    int r1 = (ew - e1 + ECH) % ECH, r2 = (ew - e2 + ECH) % ECH;
    float dl = eb[0][r1], dr = eb[1][r2];
    elp[0] += (dl - elp[0]) * 0.35f; elp[1] += (dr - elp[1]) * 0.35f;
    eb[0][ew] = sd * 0.9f + elp[1] * 0.38f;
    eb[1][ew] = sd * 0.9f + elp[0] * 0.38f;
    ew = (ew + 1) % ECH;
    L += elp[0] * 0.5f; R += elp[1] * 0.5f;
    L *= sfx_vol; R *= sfx_vol;
    /* a soft clip, so stacked effects and the music never overshoot */
    L = tanhf(L * 0.85f); R = tanhf(R * 0.85f);
    outl[i] += L * gain; outr[i] += R * gain;
  }
}

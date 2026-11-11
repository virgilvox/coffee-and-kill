/* songs.c: the dance break songs of the page ($l), composed into events by
 * P3 (chords voiced by J0 and pC, the drums of A3, the lead of _l, the OUTRO
 * appended), and charted into arrows by K0. See songs.h. */
#include "songs.h"
#include "ck.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct { const char *tag; const char *chords[4]; int nchords; const char *layers[4]; int nlayers; float its; const char *lead; } sect_t;
typedef struct { int step, semis, dur; float vel; } bassnote_t;
typedef struct {
  const char *id, *name; int bpm;
  int reg_bass[2], reg_pad[2], reg_comp[2];
  bassnote_t bass[8]; int nbass;
  sect_t form[6]; int nform;
} songdef_t;

static const songdef_t SONGS[5] = {
  {"funk", "BOILER ROOM FUNK", 112, {28, 45}, {52, 70}, {55, 72},
   {{0, 0, 2, 0.95f}, {3, 0, 1, 0.7f}, {6, 12, 2, 0.8f}, {8, 0, 2, 0.9f}, {10, 7, 2, 0.75f}, {11, 0, 1, 0.6f}, {14, 12, 2, 0.8f}}, 7,
   {{"INTRO", {"Em7", "Em7"}, 2, {"kick", "hat", "pad", "bass"}, 4, 0.3f, NULL},
    {"A", {"Em7", "Em7", "A7", "Em7"}, 4, {0}, 0, 0.65f, "E5:2 -:2 G5:2 A5:2 -:2 G5:2 E5:4 | D5:2 -:2 E5:2 G5:2 -:2 E5:2 D5:4 | C#5:2 -:2 E5:2 A5:2 -:2 G5:2 E5:4 | G5:2 E5:2 D5:2 E5:2 B4:4 -:4"},
    {"B", {"Cmaj7", "D7", "Em7", "Em7"}, 4, {0}, 0, 0.95f, "B5:4 A5:2 G5:2 E5:4 G5:4 | A5:4 G5:2 F#5:2 D5:4 F#5:4 | G5:2 A5:2 B5:4 A5:4 G5:4 | E5:8 -:8"},
    {"OUT", {"Em7", "Em7"}, 2, {0}, 0, 0.8f, "E5:2 -:2 G5:2 A5:2 -:2 G5:2 E5:4 | D5:2 -:2 E5:2 G5:2 B5:8"}}, 4},
  {"disco", "CAPSULE DISCO", 124, {33, 50}, {55, 74}, {57, 74},
   {{0, 0, 2, 0.9f}, {2, 12, 2, 0.7f}, {4, 0, 2, 0.85f}, {6, 12, 2, 0.7f}, {8, 0, 2, 0.9f}, {10, 12, 2, 0.7f}, {12, 0, 2, 0.85f}, {14, 12, 2, 0.7f}}, 8,
   {{"INTRO", {"Am7", "Dm7"}, 2, {"kick", "hat", "pad", "bass"}, 4, 0.3f, NULL},
    {"A", {"Am7", "Dm7", "G7", "Cmaj7"}, 4, {0}, 0, 0.65f, "A4:2 C5:2 E5:4 D5:2 C5:2 A4:4 | D5:2 F5:2 A5:4 G5:2 F5:2 D5:4 | G4:2 B4:2 D5:4 F5:2 D5:2 B4:4 | C5:2 E5:2 G5:4 E5:6 -:2"},
    {"HOOK", {"Fmaj7", "G7", "Am7", "Am7"}, 4, {0}, 0, 0.95f, "E5:2 A5:2 A5:2 G5:2 E5:4 C5:2 -:2 | D5:2 G5:2 G5:2 F5:2 D5:4 B4:2 -:2 | C5:2 E5:2 G5:4 E5:2 D5:2 C5:2 -:2 | A4:2 C5:2 E5:6 A5:6"},
    {"HOOK", {"Fmaj7", "G7", "Am7", "Am7"}, 4, {0}, 0, 1.0f, "E5:2 A5:2 A5:2 G5:2 E5:4 C5:2 -:2 | D5:2 G5:2 B5:2 A5:2 G5:4 E5:2 -:2 | F5:2 A5:2 C6:4 A5:2 G5:2 E5:2 -:2 | A5:8 -:8"}}, 4},
  {"rave", "CHIP RAVE", 140, {28, 45}, {52, 71}, {64, 84},
   {{2, 0, 2, 0.9f}, {6, 0, 2, 0.9f}, {10, 0, 2, 0.9f}, {14, 0, 2, 0.9f}, {0, 12, 1, 0.5f}}, 5,
   {{"INTRO", {"Em", "C"}, 2, {"kick", "hat", "pad", "bass"}, 4, 0.3f, NULL},
    {"A", {"Em", "C", "D", "Bm"}, 4, {0}, 0, 0.7f, "E5:2 G5:2 B5:2 G5:2 E5:2 G5:2 B5:4 | C5:2 E5:2 G5:2 E5:2 C5:2 E5:2 G5:4 | D5:2 F#5:2 A5:2 F#5:2 D5:2 F#5:2 A5:4 | B4:2 D5:2 F#5:2 D5:2 B4:4 -:4"},
    {"B", {"Em", "C", "G", "Em"}, 4, {0}, 0, 1.0f, "B5:4 B5:2 A5:2 G5:4 E5:4 | A5:4 A5:2 G5:2 E5:4 C5:4 | G5:2 A5:2 B5:4 D6:4 B5:4 | B5:8 -:8"},
    {"A", {"Em", "C", "D", "Bm"}, 4, {0}, 0, 0.9f, "E5:2 G5:2 B5:2 G5:2 E5:2 G5:2 B5:4 | C5:2 E5:2 G5:2 E5:2 C5:2 E5:2 G5:4 | D5:2 F#5:2 A5:2 F#5:2 D5:2 F#5:2 A5:4 | B5:8 E6:8"}}, 4},
  {"surf", "GARAGE SURF", 150, {26, 43}, {50, 69}, {40, 55},
   {{0, 0, 2, 0.9f}, {2, 0, 2, 0.75f}, {4, 0, 2, 0.85f}, {6, 0, 2, 0.75f}, {8, 0, 2, 0.9f}, {10, 0, 2, 0.75f}, {12, 0, 2, 0.85f}, {14, 7, 2, 0.75f}}, 8,
   {{"INTRO", {"Dm", "Dm"}, 2, {"kick", "hat", "comp", "bass"}, 4, 0.3f, NULL},
    {"A", {"Dm", "Dm", "Bb", "A"}, 4, {0}, 0, 0.7f, "D5:2 D5:2 F5:2 A5:2 D5:2 D5:2 F5:2 A5:2 | D5:2 D5:2 F5:2 A5:2 C6:4 A5:4 | Bb5:2 A5:2 G5:2 F5:2 G5:2 A5:2 Bb5:4 | A5:2 G5:2 E5:2 A5:2 C#6:4 -:4"},
    {"B", {"Dm", "Bb", "Gm", "A"}, 4, {0}, 0, 1.0f, "F5:4 A5:4 D6:4 C6:4 | Bb5:4 A5:4 F5:4 A5:4 | G5:2 A5:2 Bb5:4 A5:4 G5:4 | A5:8 -:8"},
    {"A", {"Dm", "Dm", "Bb", "A"}, 4, {0}, 0, 0.9f, "D5:2 D5:2 F5:2 A5:2 D5:2 D5:2 F5:2 A5:2 | D5:2 D5:2 F5:2 A5:2 C6:4 A5:4 | Bb5:2 A5:2 G5:2 F5:2 G5:2 A5:2 Bb5:4 | D6:8 A5:8"}}, 4},
  {"lounge", "MIDNIGHT LOUNGE", 108, {31, 48}, {53, 72}, {60, 79},
   {{0, 0, 3, 0.85f}, {6, 7, 2, 0.7f}, {8, 0, 2, 0.8f}, {11, 12, 2, 0.65f}, {14, 7, 2, 0.7f}}, 5,
   {{"INTRO", {"Fmaj7", "Am7"}, 2, {"kick", "hat", "pad", "bass"}, 4, 0.3f, NULL},
    {"A", {"Fmaj7", "Am7", "Gm7", "C7"}, 4, {0}, 0, 0.65f, "C5:2 F5:2 A5:4 G5:2 F5:2 E5:4 | E5:2 A5:2 C6:4 Bb5:2 A5:2 G5:4 | D5:2 G5:2 Bb5:4 A5:2 G5:2 E5:4 | E5:4 G5:4 Bb5:8"},
    {"B", {"Dm7", "Gm7", "C7", "Fmaj7"}, 4, {0}, 0, 0.9f, "D5:2 F5:2 A5:4 G5:4 F5:4 | D5:2 G5:2 Bb5:4 A5:4 G5:4 | E5:2 G5:2 C6:4 Bb5:4 G5:4 | A5:8 F5:8"}}, 3},
};

/* W0 */
static int pitch_class(const char *s, int *len) {
  static const int base[7] = {9, 11, 0, 2, 4, 5, 7};   /* A B C D E F G */
  if (*s < 'A' || *s > 'G') return -1;
  int pc = base[*s - 'A'], n = 1;
  if (s[1] == '#') { pc++; n++; } else if (s[1] == 'b') { pc--; n++; }
  *len = n;
  return (pc + 12) % 12;
}
/* ql: "C#5" -> 12 * (octave + 1) + pc (the name's own pc, so "Cb4" is not wrapped: W0 has no Cb) */
static int note_midi(const char *s, int n) {
  int l; int pc = pitch_class(s, &l);
  if (pc < 0 || l >= n) return -1;
  int oct = atoi(s + l);
  /* W0 maps names to 0..11 as written (C#: 1, Db: 1, Bb: 10, ...); no name in $l wraps */
  return 12 * (oct + 1) + pc;
}
static int js_round(double x) { return (int)floor(x + 0.5); }      /* Math.round */
static int pC(int s, double a) { return s + 12 * js_round((a - s) / 12.0); }

typedef struct { int root; int ivs[4]; int n; } chord_t;
static chord_t Kl(const char *s) {
  chord_t c; int l;
  c.root = pitch_class(s, &l);
  const char *q = s + l;
  if (!strcmp(q, "m")) { int v[] = {0, 3, 7}; memcpy(c.ivs, v, sizeof v); c.n = 3; }
  else if (!strcmp(q, "7")) { int v[] = {0, 4, 7, 10}; memcpy(c.ivs, v, sizeof v); c.n = 4; }
  else if (!strcmp(q, "maj7")) { int v[] = {0, 4, 7, 11}; memcpy(c.ivs, v, sizeof v); c.n = 4; }
  else if (!strcmp(q, "m7")) { int v[] = {0, 3, 7, 10}; memcpy(c.ivs, v, sizeof v); c.n = 4; }
  else { int v[] = {0, 4, 7}; memcpy(c.ivs, v, sizeof v); c.n = 3; }
  return c;
}
/* J0: voice the chord in [lo, hi], each note near the previous voicing's same index */
static int J0(int root, const chord_t *k, const int *prev, int nprev, int lo, int hi, int *out) {
  for (int w = 0; w < k->n && w < 4; w++) {
    int g = (root + k->ivs[w]) % 12;
    double i = prev && w < nprev ? prev[w] : lo + (w + 0.5) * (hi - lo) / 4.0;
    int n = pC(g, i);
    while (n < lo) n += 12;
    while (n > hi) n -= 12;
    out[w] = n;
  }
  return k->n < 4 ? k->n : 4;
}

typedef struct { song_ev *e; int n, cap; } evlist;
static void oe(evlist *L, int ch, int step, int midi, int nm, const int *midis, float dur, float vel) {
  if (L->n == L->cap) { L->cap = L->cap ? L->cap * 2 : 512; L->e = realloc(L->e, sizeof(song_ev) * L->cap); }
  song_ev *v = &L->e[L->n++];
  memset(v, 0, sizeof *v);
  v->step = step; v->ch = ch; v->midi = midi; v->nm = nm; v->dur = dur; v->vel = vel;
  for (int i = 0; i < nm && i < 6; i++) v->midis[i] = midis[i];
}

/* A3: one sixteenth of drums */
static void A3(evlist *t, const char *id, int bar, int e, float o, bool fill, bool intro) {
  int st = bar * 16 + e;
  #define K(v) oe(t, CH_KICK, st, 36, 0, NULL, 2, v)
  #define S(v) oe(t, CH_SNARE, st, 38, 0, NULL, 2, v)
  #define CL(v) oe(t, CH_CLAP, st, 39, 0, NULL, 2, v)
  #define H(v) oe(t, CH_HAT, st, 42, 0, NULL, 2, v)
  #define OH(v) oe(t, CH_OHAT, st, 46, 0, NULL, 2, v)
  #define TOM(m, v) oe(t, CH_TOM, st, m, 0, NULL, 2, v)
  if (intro) { if (e % 4 == 0) K(0.85f); if (e % 4 == 2) H(0.4f); return; }
  bool B = o > 0.8f;
  if (!strcmp(id, "funk")) {
    if (e == 0 || e == 3 || e == 8 || e == 10 || (B && e == 14)) K(0.95f);
    if (e == 4 || e == 12) { S(0.85f); if (B) CL(0.45f); }
    if (e % 2 == 0) H(e % 4 == 0 ? 0.5f : 0.34f); else if (B) H(0.18f);
    if (fill && e >= 12) TOM(e % 2 ? 53 : 50, 0.7f);
  } else if (!strcmp(id, "disco")) {
    if (e % 4 == 0) K(0.95f);
    if (e == 4 || e == 12) { CL(0.7f); if (B) S(0.5f); }
    if (e % 4 == 2) OH(0.55f); else if (e % 2 == 0) H(0.28f);
    if (fill && e >= 12) S(0.4f + (e - 12) * 0.12f);
  } else if (!strcmp(id, "rave")) {
    if (e % 4 == 0) K(1);
    if (e == 4 || e == 12) CL(0.7f);
    if (e % 4 == 2) H(0.55f); else if (e % 2 == 1 && B) H(0.2f);
    if (fill && e >= 12) S(0.4f + (e - 12) * 0.13f);
  } else if (!strcmp(id, "surf")) {
    if (e == 0 || e == 8 || (e == 10 && bar % 2)) K(0.95f);
    if (e == 4 || e == 12) S(0.9f);
    if (e % 2 == 0) H(0.4f); else if (B) H(0.2f);
    if (bar % 4 == 3 && e >= 12) TOM(e % 2 ? 53 : 50, 0.7f);
  } else if (!strcmp(id, "lounge")) {
    if (e == 0 || e == 10 || (B && e == 6)) K(0.7f);
    if (e == 4 || e == 12) S(0.5f);
    if (e % 2 == 0) H(e % 4 == 0 ? 0.32f : 0.2f);
  }
  #undef K
  #undef S
  #undef CL
  #undef H
  #undef OH
  #undef TOM
}

static bool has_layer(const sect_t *g, const char *l) {
  if (!g->nlayers) return true;   /* no layers: everything */
  for (int i = 0; i < g->nlayers; i++) if (!strcmp(g->layers[i], l)) return true;
  return false;
}

static int cmp_i(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static void compose(const songdef_t *P, song_t *S) {
  sect_t form[7]; int nform = P->nform;
  memcpy(form, P->form, sizeof(sect_t) * nform);
  const sect_t *last = &P->form[nform - 1];
  const char *lc = last->chords[last->nchords - 1];
  sect_t outro = {"OUTRO", {lc, lc}, 2, {"pad", "bass"}, 2, 0.4f, NULL};
  form[nform++] = outro;
  evlist t = {0};
  int D = 0;
  for (int gi = 0; gi < nform; gi++) {
    const sect_t *g = &form[gi];
    int r = g->nchords; bool v = gi == 0; float o = g->its;
    int a[4]; int na = 0; bool have_a = false;
    for (int c = 0; c < r; c++) {
      chord_t Q = Kl(g->chords[c]);
      int m = D + c, j = m * 16;
      int f[4]; int nf = J0(Q.root, &Q, have_a ? a : NULL, na, P->reg_pad[0], P->reg_pad[1], f);
      memcpy(a, f, sizeof f); na = nf; have_a = true;
      int u = pC(Q.root, (P->reg_bass[0] + P->reg_bass[1]) / 2.0);
      int T[4]; int nT = J0(Q.root, &Q, NULL, 0, P->reg_comp[0], P->reg_comp[1], T);
      if (has_layer(g, "pad") && strcmp(P->id, "surf") && (strcmp(P->id, "funk") || v || o > 0.9f))
        oe(&t, CH_PAD, j, 0, nf, f, 15, !strcmp(P->id, "lounge") ? 0.5f : 0.55f);
      if (has_layer(g, "bass") && (o >= 0.6f || v))
        for (int b = 0; b < P->nbass; b++) {
          const bassnote_t *bn = &P->bass[b];
          if (v && bn->step != 0 && bn->step != 8) continue;
          int N = u + bn->semis;
          while (N > P->reg_bass[1] + 12) N -= 12;
          while (N < P->reg_bass[0]) N += 12;
          oe(&t, CH_BASS, j + bn->step, N, 0, NULL, (float)bn->dur, bn->vel * (0.8f + 0.2f * o));
        }
      bool comp_layer = g->nlayers && has_layer(g, "comp");
      if (!v || comp_layer) {
        if (!strcmp(P->id, "funk")) {
          static const int at[4] = {3, 6, 10, 13};
          for (int k = 0; k < 4; k++) oe(&t, CH_COMP, j + at[k], 0, nT, T, 1, 0.42f + 0.1f * o);
          if (o > 0.6f) {
            int T12[4]; for (int k = 0; k < nT; k++) T12[k] = T[k] + 12;
            if (c % 2 == 0) oe(&t, CH_STAB, j, 0, nT, T12, 4, 0.5f);
            oe(&t, CH_STAB, j + 10, 0, nT, T12, 2, 0.42f);
          }
        } else if (!strcmp(P->id, "disco")) {
          static const int at[4] = {2, 6, 10, 14};
          for (int k = 0; k < 4; k++) oe(&t, CH_COMP, j + at[k], 0, nT, T, 1, 0.4f + 0.1f * o);
          if (o > 0.9f) {
            int T12[4]; for (int k = 0; k < nT; k++) T12[k] = T[k] + 12;
            oe(&t, CH_STAB, j + 6, 0, nT, T12, 2, 0.42f);
            oe(&t, CH_STAB, j + 14, 0, nT, T12, 2, 0.42f);
          }
        } else if (!strcmp(P->id, "rave")) {
          int I[5]; int nI = nT;
          for (int k = 0; k < nT; k++) I[k] = T[k];
          I[nI++] = T[0] + 12;
          qsort(I, nI, sizeof(int), cmp_i);
          for (int h = 0; h < 16; h += 2) oe(&t, CH_ARP, j + h, I[(h >> 1) % nI] + 12, 0, NULL, 2, 0.35f + 0.25f * o);
        } else if (!strcmp(P->id, "surf")) {
          int I = pC(Q.root, 47);
          for (int h = 0; h < 16; h += 2) { int mm[2] = {I, I + 7}; oe(&t, CH_COMP, j + h, 0, 2, mm, 2, h % 4 == 0 ? 0.85f : 0.6f); }
        } else if (!strcmp(P->id, "lounge")) {
          static const int at[3] = {0, 6, 10};
          for (int k = 0; k < 3; k++) oe(&t, CH_COMP, j + at[k], 0, nT, T, at[k] == 0 ? 6.f : 3.f, 0.4f);
          if (o > 0.8f) for (int I = 2; I < 16; I += 4) oe(&t, CH_ARP, j + I, T[(I >> 2) % nT] + 12, 0, NULL, 2, 0.35f);
        }
      }
      for (int I = 0; I < 16; I++) A3(&t, P->id, m, I, o, c == r - 1 && !v, v);
    }
    if (g->lead) {   /* _l(g.lead, D * 16, .85) */
      const char *p = g->lead; int tt = 0;
      while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *q = p; while (*q && *q != ' ') q++;
        int len = (int)(q - p);
        if (len == 1 && *p == '|') tt = js_round(tt / 16.0) * 16;
        else {
          const char *colon = memchr(p, ':', len);
          if (colon) {
            int g2 = atoi(colon + 1);
            if (!(colon - p == 1 && *p == '-')) {
              int midi = note_midi(p, (int)(colon - p));
              if (midi >= 0) oe(&t, CH_LEAD, D * 16 + tt, midi, 0, NULL, (float)g2, 0.85f);
            }
            tt += g2;
          }
        }
        p = q;
      }
    }
    D += r;
  }
  /* sort by step, stable (the push order within a step) */
  song_ev *s = malloc(sizeof(song_ev) * (t.n ? t.n : 1));
  int total = D * 16;
  int *cnt = calloc((size_t)total + 2, sizeof(int));
  for (int i = 0; i < t.n; i++) cnt[t.e[i].step + 1]++;
  for (int i = 1; i <= total + 1; i++) cnt[i] += cnt[i - 1];
  S->step_first = malloc(sizeof(int) * ((size_t)total + 1));
  for (int i = 0; i <= total; i++) S->step_first[i] = cnt[i];
  for (int i = 0; i < t.n; i++) s[cnt[t.e[i].step]++] = t.e[i];
  free(cnt); free(t.e);
  S->id = P->id; S->name = P->name; S->bpm = P->bpm;
  S->sec_per_step = 60.f / P->bpm / 4.f;
  S->total_bars = D; S->total_steps = D * 16;
  S->body_bars = D - 2 - 2; S->song_beats = (D - 2 - 2) * 4;
  S->ev = s; S->nev = t.n;
}

static song_t HC[5];
static bool made;
static void make(void) {
  if (made) return;
  for (int i = 0; i < 5; i++) compose(&SONGS[i], &HC[i]);
  made = true;
}

int song_count(void) { return 5; }
const song_t *song_get(int i) { make(); return i >= 0 && i < 5 ? &HC[i] : NULL; }
const song_t *song_from_seed(uint32_t seed) { make(); return &HC[seed % 5]; }

void song_pick(int s, uint32_t *seed, int *idx) {
  int A = 5, P;
  if (s < 0) P = (int)floorf(ck_rand() * A);
  else P = (s + 1 + (int)floorf(ck_rand() * (A - 1))) % A;
  *idx = P;
  *seed = (uint32_t)(P + A * (uint32_t)floor(ck_rand() * 4e6));
}

int song_chart(const song_t *s, chart_note *out, int max) {
  int n = 0, P = -1, e = 2, t = 0, D = -9;
  for (int g = 0; g < s->total_steps; g++) {
    /* the first lead event of each step, steps ascending */
    int first = -1;
    for (int i = s->step_first[g]; i < s->step_first[g + 1]; i++) if (s->ev[i].ch == CH_LEAD) { first = i; break; }
    if (first < 0) continue;
    if (g < 32 || g - D < 2) continue;
    int i = s->ev[first].midi, lane;
    if (P < 0) lane = 2;
    else if (i > P) lane = e == 2 ? 3 : 2;
    else if (i < P) lane = e == 1 ? 0 : 1;
    else { t ^= 1; lane = t ? 3 : 0; if (lane == e) lane = lane == 3 ? 0 : 3; }
    P = i; e = lane; D = g;
    if (n < max) { out[n].beat = g / 4.f; out[n].lane = lane; }
    n++;
  }
  return n < max ? n : max;
}

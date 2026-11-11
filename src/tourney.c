/* tourney.c: see tourney.h. */
#include "tourney.h"
#include "roster.h"
#include "weapons.h"
#include <string.h>

static uint32_t rng(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

static int default_weapon(int fighter) { int w = weapon_index(roster_get(fighter)->weapon); return w < 0 ? 0 : w; }

static bool taken(const tourney_t *t, int upto, int fighter) {
  for (int i = 0; i < upto; i++) if (t->e[i].fighter == fighter) return true;
  return false;
}

void tourney_defaults(tourney_t *t, int size) {
  memset(t, 0, sizeof *t);
  t->stage = -1; t->cur = -1; t->champion = -1; t->seed = 0x5eed1234u;
  for (int i = 0; i < TOURNEY_MAX; i++) {
    t->e[i].fighter = i % ROSTER_N;
    t->e[i].weapon = default_weapon(t->e[i].fighter);
    t->e[i].ctl = i == 0 ? TC_PLAYER : TC_JOURNEYMAN;
  }
  tourney_set_size(t, size);
}

void tourney_set_size(tourney_t *t, int size) { t->size = size <= 4 ? 4 : 8; }

void tourney_cycle_ctl(tourney_t *t, int i, int d) {
  if (i < 0 || i >= t->size) return;
  t->e[i].ctl = (t->e[i].ctl + d + TC_N) % TC_N;
}
void tourney_cycle_fighter(tourney_t *t, int i, int d) {
  if (i < 0 || i >= t->size) return;
  t->e[i].fighter = (t->e[i].fighter + d + ROSTER_N) % ROSTER_N;
  t->e[i].weapon = default_weapon(t->e[i].fighter);
}
void tourney_set_fighter(tourney_t *t, int i, int f) {
  if (i < 0 || i >= t->size || f < 0 || f >= ROSTER_N) return;
  t->e[i].fighter = f; t->e[i].weapon = default_weapon(f);
}
void tourney_cycle_weapon(tourney_t *t, int i, int d) {
  if (i < 0 || i >= t->size) return;
  t->e[i].weapon = (t->e[i].weapon + d + WEAPON_N) % WEAPON_N;
}
void tourney_shuffle_cpus(tourney_t *t) {
  for (int i = 0; i < t->size; i++) {
    if (t->e[i].ctl == TC_PLAYER) continue;
    int f, tries = 0;
    do f = (int)(rng(&t->seed) % (uint32_t)ROSTER_N); while (taken(t, i, f) && ++tries < 32);
    t->e[i].fighter = f; t->e[i].weapon = default_weapon(f);
  }
}

int tourney_matches(const tourney_t *t) { return t->size - 1; }
int tourney_rounds(const tourney_t *t) { return t->size == 8 ? 3 : 2; }
int tourney_round_first(const tourney_t *t, int r) {
  int first = 0, n = t->size / 2;
  for (int k = 0; k < r; k++) { first += n; n /= 2; }
  return first;
}
int tourney_round_of(const tourney_t *t, int m) {
  for (int r = tourney_rounds(t) - 1; r >= 0; r--) if (m >= tourney_round_first(t, r)) return r;
  return 0;
}
const char *tourney_round_name(const tourney_t *t, int r) {
  int left = tourney_rounds(t) - r;
  return left == 1 ? "FINAL" : left == 2 ? "SEMIFINALS" : "QUARTERFINALS";
}

void tourney_start(tourney_t *t, uint32_t seed) {
  t->seed = seed ? seed : 0x5eed1234u;
  t->started = true; t->champion = -1; t->cur = 0;
  int order[TOURNEY_MAX];
  for (int i = 0; i < t->size; i++) order[i] = i;
  for (int i = t->size - 1; i > 0; i--) { int j = (int)(rng(&t->seed) % (uint32_t)(i + 1)); int x = order[i]; order[i] = order[j]; order[j] = x; }
  /* people meet the CPU before each other where the draw allows: a person first on each side */
  for (int m = 0; m < tourney_matches(t); m++) { t->m[m].a = t->m[m].b = -1; t->m[m].winner = -1; t->m[m].wins_a = t->m[m].wins_b = 0; }
  for (int k = 0; k < t->size / 2; k++) {
    t_match *M = &t->m[k];
    M->a = order[k * 2]; M->b = order[k * 2 + 1];
    if (t->e[M->a].ctl != TC_PLAYER && t->e[M->b].ctl == TC_PLAYER) { int x = M->a; M->a = M->b; M->b = x; }
  }
}

int tourney_stage_of(const tourney_t *t, int m, int n) {
  if (t->stage >= 0) return t->stage;
  return n > 0 ? (int)((t->seed % 7u + (uint32_t)m * 3u) % (uint32_t)n) : 0;
}

static void advance(tourney_t *t) {
  int m = t->cur;
  t_match *M = &t->m[m];
  int r = tourney_round_of(t, m);
  if (r == tourney_rounds(t) - 1) { t->champion = M->winner; t->cur = -1; return; }
  int first = tourney_round_first(t, r), nf = tourney_round_first(t, r + 1);
  t_match *N = &t->m[nf + (m - first) / 2];
  if ((m - first) % 2 == 0) N->a = M->winner; else N->b = M->winner;
  /* a person on the left: they get the P1 side of the controls */
  if (N->a >= 0 && N->b >= 0 && t->e[N->a].ctl != TC_PLAYER && t->e[N->b].ctl == TC_PLAYER) {
    int x = N->a; N->a = N->b; N->b = x;
  }
  t->cur = m + 1;
}

void tourney_report(tourney_t *t, int side, int wins_a, int wins_b) {
  if (!t->started || t->cur < 0) return;
  t_match *M = &t->m[t->cur];
  M->winner = side ? M->b : M->a;
  M->wins_a = wins_a; M->wins_b = wins_b;
  advance(t);
}

bool tourney_cpu_only(const tourney_t *t, int m) {
  if (m < 0) return false;
  const t_match *M = &t->m[m];
  return M->a >= 0 && M->b >= 0 && t->e[M->a].ctl != TC_PLAYER && t->e[M->b].ctl != TC_PLAYER;
}

void tourney_sim(tourney_t *t) {
  if (t->cur < 0) return;
  t_match *M = &t->m[t->cur];
  int la = t->e[M->a].ctl, lb = t->e[M->b].ctl;   /* TC_ROOKIE 1 .. TC_EXTREME 3 */
  uint32_t pa = 2 + (uint32_t)la, pb = 2 + (uint32_t)lb;
  bool a = rng(&t->seed) % (pa + pb) < pa;
  int loser = (int)(rng(&t->seed) % 2);
  tourney_report(t, a ? 0 : 1, a ? 2 : loser, a ? loser : 2);
}

int tourney_humans(const tourney_t *t) { int n = 0; for (int i = 0; i < t->size; i++) n += t->e[i].ctl == TC_PLAYER; return n; }
const char *tourney_ctl_name(int c) {
  static const char *N[TC_N] = {"PLAYER", "CPU ROOKIE", "CPU JOURNEYMAN", "CPU EXTREME"};
  return c >= 0 && c < TC_N ? N[c] : "";
}

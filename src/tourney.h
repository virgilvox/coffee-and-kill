/* tourney.h: the tourney's bracket, as data. Four or eight entrants, each a fighter with
 * its weapon and who plays it (a person or the CPU at a level); single elimination, every
 * match the usual best of three rounds. Nothing here draws or fights: the setup and bracket
 * screens (ui_menus.c) show and edit it, game.c plays the next match and reports the result.
 *
 * Matches are numbered round by round: for eight entrants 0..3 are the quarterfinals, 4 and 5
 * the semifinals, 6 the final. The winner of match m in a round goes to the next round's match
 * (m - first) / 2, on side (m - first) % 2. */
#ifndef CK_TOURNEY_H
#define CK_TOURNEY_H
#include <stdbool.h>
#include <stdint.h>

#define TOURNEY_MAX 8
enum { TC_PLAYER, TC_ROOKIE, TC_JOURNEYMAN, TC_EXTREME, TC_N };   /* who plays an entrant */

typedef struct {
  int fighter, weapon;          /* roster and weapon indices */
  int ctl;                      /* TC_* */
} t_entrant;

typedef struct {
  int a, b;                     /* entrants, -1 until the round before decides */
  int winner;                   /* -1 until played; else a or b */
  int wins_a, wins_b;           /* rounds won */
} t_match;

typedef struct {
  int size;                     /* 4 or 8 */
  t_entrant e[TOURNEY_MAX];
  int stage;                    /* a stage index, or -1: a different stage each match */
  /* the bracket, once started */
  bool started;
  t_match m[TOURNEY_MAX - 1];
  int cur;                      /* the match to play next, -1 when the final is done */
  int champion;                 /* an entrant, -1 until the final */
  uint32_t seed;
} tourney_t;

void tourney_defaults(tourney_t *t, int size);   /* P1 on the first slot, CPUs after, different fighters */
void tourney_set_size(tourney_t *t, int size);
void tourney_cycle_ctl(tourney_t *t, int i, int d);
void tourney_cycle_fighter(tourney_t *t, int i, int d);   /* the fighter's own weapon comes with it */
void tourney_cycle_weapon(tourney_t *t, int i, int d);
void tourney_set_fighter(tourney_t *t, int i, int fighter);   /* with its own weapon */
void tourney_shuffle_cpus(tourney_t *t);         /* new fighters for the CPU entrants */

void tourney_start(tourney_t *t, uint32_t seed);  /* seeds the first round in a shuffled order */
int tourney_matches(const tourney_t *t);          /* size - 1 */
int tourney_rounds(const tourney_t *t);           /* 2 or 3 */
int tourney_round_of(const tourney_t *t, int m);  /* 0 = the first round */
int tourney_round_first(const tourney_t *t, int r);
const char *tourney_round_name(const tourney_t *t, int r);   /* "QUARTERFINALS", "SEMIFINALS", "FINAL" */
/* the stage of match m (the tourney's, or the rotation's) */
int tourney_stage_of(const tourney_t *t, int m, int stage_count);
/* the result of the current match: side 0 (a) or 1 (b) won, with the rounds each took */
void tourney_report(tourney_t *t, int side, int wins_a, int wins_b);
bool tourney_cpu_only(const tourney_t *t, int m); /* nobody at the controls: it can be skipped */
/* a CPU against CPU match decided without playing it: the better level is likelier to win */
void tourney_sim(tourney_t *t);
int tourney_humans(const tourney_t *t);
const char *tourney_ctl_name(int ctl);           /* "PLAYER", "CPU ROOKIE", ... */

#endif

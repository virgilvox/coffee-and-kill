/* songs.h: the dance break songs ($l through P3 in the page) and their
 * arrow charts (K0). Plain data, shared by music.cpp (which plays them, rl())
 * and dance.c (which charts and judges them). */
#ifndef CK_SONGS_H
#define CK_SONGS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { CH_LEAD, CH_HARM, CH_PAD, CH_BASS, CH_COMP, CH_STAB, CH_ARP,
       CH_KICK, CH_SNARE, CH_CLAP, CH_HAT, CH_OHAT, CH_TOM, CH_N };

/* one event of P3's t.<channel>[step] lists: a note (midi, dur in steps, vel)
 * or a chord (midis[0..nm)) */
typedef struct { int step, ch, midi, nm; float dur, vel; int midis[6]; } song_ev;

typedef struct {
  const char *id, *name;
  int bpm;
  float sec_per_step;          /* 60 / bpm / 4 */
  int total_bars, total_steps, body_bars, song_beats;
  song_ev *ev; int nev;        /* sorted by step, then in P3's push order */
  int *step_first;             /* total_steps + 1: index of the first event at each step */
} song_t;

int song_count(void);                        /* HC.length (5) */
const song_t *song_get(int idx);
const song_t *song_from_seed(uint32_t seed); /* V0(seed) */
/* q0(lastIdx): the next dance's seed and index (never the same song twice running) */
void song_pick(int last_idx, uint32_t *seed, int *idx);

typedef struct { float beat; int lane; } chart_note;
/* K0(track): the arrow chart of a song; returns the count (<= max) */
int song_chart(const song_t *s, chart_note *out, int max);

#ifdef __cplusplus
}
#endif
#endif

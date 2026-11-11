/* dance.h: the dance break between rounds (sv of the page): a four lane
 * arrow chart over a dance song, one or two players (or a CPU in one player
 * against no one: solo grades), judged PERFECT / GREAT / OK / MISS, drawn on
 * its own canvas over everything (#dance, z-index 25). */
#ifndef CK_DANCE_H
#define CK_DANCE_H
#include <stdbool.h>
#include <stdint.h>
#include "mp.h"

enum { DP_HUMAN, DP_CPU, DP_REMOTE };
typedef struct { const char *name; int kind; int input_slot; float acc; } dance_player;
typedef struct {
  bool solo, skip; int winner; char grade[2]; int meter; float acc;
  int scores[2], combos[2];
} dance_result;

void dance_init(void);
/* start(seed, players, net, touchBtns): net sends the "dh"/"df" messages online (NULL offline) */
void dance_start(uint32_t seed, const dance_player *players, int n, void (*net_send)(mp_val *msg));
void dance_update(float dt);
void dance_draw(void);
bool dance_active(void);
void dance_cancel(void);
float dance_beat_pulse(void);
/* the result, once the song and the 2.8 s result card are done */
bool dance_take_result(dance_result *out);
/* onMove(player, lane, judgement): the fighter dances */
extern void (*dance_on_move)(int player, int lane, int judgement);
/* the remote player's judgements (online) */
void dance_remote_hit(int i, int j, int score);
void dance_remote_final(int score, int max_combo);

#endif

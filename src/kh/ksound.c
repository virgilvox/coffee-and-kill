/* ksound.c: KEYHOLDER's music and effects in the page (see ksound.h). kaudio.c is the synth; this only
 * decides when it plays. The page's own sounds always play too (online guests replay them by name), so on a
 * KEYHOLDER stage its effects are layered on top, and the page's band steps aside for its fight music. */
#include "ksound.h"
#include "kaudio.h"
#include "kweapon.h"
#include <string.h>

static bool kh_stage;
static int swing_kh = -1;

void ksound_round(bool kh, bool final_round) {
  kh_stage = kh;
  kmusic_mood(!kh ? KMOOD_SILENT : final_round ? KMOOD_BOSS : KMOOD_FIGHT);
}
void ksound_match_end(void) { if (kh_stage) kmusic_mood(KMOOD_CLEAR); }
void ksound_menu(void) { kh_stage = false; kmusic_mood(KMOOD_SILENT); }
void ksound_weapon(int kh) { swing_kh = kh; }

static ksfx_t swing_of(int kh) {
  switch (kh) {
    case WP_ARC: return KSFX_SWING_ARC; case WP_MAUL: return KSFX_SWING_MAUL; case WP_TWIN: return KSFX_SWING_TWIN;
    case WP_SPEAR: return KSFX_SWING_SPEAR; default: return KSFX_BOLT;
  }
}

void ksound_echo(const char *n, float a) {
  if (!strcmp(n, "whoosh")) { if (swing_kh >= 0) ksfx(swing_of(swing_kh), 0, .8f); swing_kh = -1; return; }
  if (!kh_stage) return;
  if (!strcmp(n, "hit")) ksfx(a > .5f ? KSFX_HIT_HEAVY : KSFX_HIT_LIGHT, 0, .7f);
  else if (!strcmp(n, "block")) ksfx(KSFX_CLASH, 0, .7f);
  else if (!strcmp(n, "ko")) ksfx(KSFX_KO, 0, .8f);
  else if (!strcmp(n, "shock")) ksfx(KSFX_SHOCK, 0, .7f);
  else if (!strcmp(n, "special")) ksfx(KSFX_SPECIAL_RELEASE, 0, .8f);
  else if (!strcmp(n, "fight")) ksfx(KSFX_SMASH, 0, .7f);
  else if (!strcmp(n, "dodge")) ksfx(KSFX_DODGE, 0, .6f);
}

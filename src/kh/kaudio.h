/* kaudio.h: KEYHOLDER's synth (src/kh/kaudio.c, from games/keyholder/src/audio.c) beside the page's own
 * sound: its effects for the KEYHOLDER weapons and its fight music on the KEYHOLDER stages. Names carry a
 * K so they never meet the page's sfx_* and music_*. kaudio_mix adds it into the page's mix block (sfx.c). */
#ifndef CK_KAUDIO_H
#define CK_KAUDIO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
  KSFX_SWING_ARC,    /* plasma through the air: a crackle and a sizzle */
  KSFX_SWING_MAUL,   /* a great weight: a slow low whoosh */
  KSFX_SWING_TWIN,   /* a quick thin whip */
  KSFX_SWING_SPEAR,  /* a long airy sweep */
  KSFX_BOLT, KSFX_LANCE,
  KSFX_CHARGE,       /* a rising whine (call every frame while charging, gain = charge) */
  KSFX_GLINT,        /* a foe's windup: a bright ting so it can be heard coming */
  KSFX_HIT_LIGHT, KSFX_HIT_HEAVY,
  KSFX_CLASH,        /* blocked: steel on steel */
  KSFX_PARRY,        /* a ringing bell of a parry */
  KSFX_GUARD_BREAK,
  KSFX_BODY_FALL,
  KSFX_SHOCK,        /* a slam into the ground */
  KSFX_ROAR,         /* a boss turning up the heat */
  KSFX_DODGE, KSFX_PERFECT,
  KSFX_SUMMON,       /* a weapon appears */
  KSFX_SPECIAL_CHARGE, KSFX_SPECIAL_RELEASE,
  KSFX_KO,
  KSFX_JUMP, KSFX_LAND, KSFX_STEP,
  KSFX_PICKUP, KSFX_WEAPON, KSFX_ALARM,
  KSFX_UI, KSFX_UI_OK,
  /* v1.1 combat */
  KSFX_SMASH,        /* the title card's smash cut: a hard hit and a ring */
  KSFX_TELL,         /* a boss about to do something big: a low swell and a sting */
  KSFX_TRAIN,        /* the light rail coming: its bell, then the horn */
  KSFX_HISS,         /* gas or steam escaping */
  KSFX_SLAM,         /* a door slammed, a stop button struck: a big flat thud and a rattle */
  KSFX_ASSIST,       /* an ally called in */
  KSFX_STYLE,        /* style points: a bright tick that climbs with the rank (gain sets the pitch) */
  KSFX_QTE,          /* the finisher's prompt: a held breath */
  KSFX_TRAIN_BELL,   /* the light rail's double ding, placed with sfx_at */
  /* v1.2 roam */
  KSFX_BIKE_BELL,    /* a thumb bell: two bright dings */
  KSFX_BIKE_TICK,    /* the freewheel's pawl, one click (call it at the hub's rate while coasting) */
  KSFX_SHEATHE,      /* a weapon put away: a soft falling shimmer */
  KSFX_COUNT
} ksfx_t;

/* v1.1: KMOOD_EXPLORE is the street by day; HOSTING the lab by day; TENSION a scene turning; NIGHT the quiet
 * late hours; ZONE the Battle Zone menu and ladder. KMOOD_SILENT stays last. */
typedef enum { KMOOD_TITLE, KMOOD_EXPLORE, KMOOD_FIGHT, KMOOD_BOSS, KMOOD_CLEAR, KMOOD_CINE, KMOOD_HOSTING, KMOOD_TENSION, KMOOD_NIGHT, KMOOD_ZONE, KMOOD_FINAL, KMOOD_SILENT } kmood_t;

void kaudio_init(uint32_t rate);
void kaudio_mix(float *l, float *r, int n, float gain);   /* adds n frames (at the init rate) into l and r */
void ksfx(ksfx_t s, float pan, float gain);              /* pan -1..1, gain 0..1 */
void kmusic_mood(kmood_t m);                             /* KMOOD_SILENT hands the music back to the page */
int kmusic_playing(void);                                /* a mood other than silent is up (or fading) */
#ifdef __cplusplus
}
#endif
#endif

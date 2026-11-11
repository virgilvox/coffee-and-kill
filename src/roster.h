/* roster.h: who can fight. Each fighter is one entry in ROSTER (roster.c): a name and
 * a look for the select screen, a default weapon, and a body.
 *
 * Bodies come in two kinds (body.h):
 *   BODY_PAGE  the web version's own rigs (MOHEEB, LUIS): a glb and its baked clips
 *   BODY_KH    a KEYHOLDER body (src/kh/actor.c): a VRoid bake, cel lit with an outline,
 *              playing assets/anim/rig_vroid.bin (tools/rig-vroid.mjs)
 * Every body plays the same move set (fighter.c MOVES) with the same clip timings.
 *
 * To add a fighter: add an entry below. A KEYHOLDER body that is new to this game also needs
 * its files: run tools/import-kh.mjs, which copies every body the roster names. */
#ifndef CK_ROSTER_H
#define CK_ROSTER_H
#include <stdbool.h>
#include <stdint.h>

enum { BODY_PAGE, BODY_KH };

typedef struct { float hue, sat, val; } roster_swatch;   /* a KEYHOLDER palette swap (actor_palette) */

typedef struct {
  const char *key;            /* the wire and the save ("moheeb", "vee"): never change one */
  const char *name;           /* "MOHEEB" */
  const char *title;          /* a few words under the name */
  const char *bio;            /* the select card's line */
  uint32_t color;             /* the fighter's accent */
  const char *weapon;         /* default weapon key (weapons.h) */
  const char *win_clip;       /* the victory clip (NULL: Celebration) */
  bool web;                   /* in the web version's roster: can face the web page online */
  int kind;                   /* BODY_PAGE or BODY_KH */
  /* BODY_PAGE */
  const char *glb, *rig;      /* "char_b.glb", "rig_b.bin" */
  float grip[3];              /* the hand's grip axis (the page's kl) */
  /* BODY_KH */
  const char *body;           /* KEYHOLDER's body letter(s): "C", "E2" (src/kh/actor.h AM_*) */
  roster_swatch pal[4];       /* hair, top, bottom, shoes; zero entries mean no change */
  /* both */
  float scale;                /* the model's scale (1 = its own size) */
} roster_entry;

extern const roster_entry ROSTER[];
extern const int ROSTER_N;

int roster_index(const char *key);        /* -1 if unknown */
const roster_entry *roster_get(int i);    /* clamped */
/* the fighters the web page knows: MOHEEB and LUIS */
int roster_web_count(void);

#endif

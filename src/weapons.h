/* weapons.h: the weapon rack. The web version's four (rs of the page: pipe wrench, big
 * spanner, sledge, crowbar) and KEYHOLDER's five (Arc Blade, Ball Peen Maul, Deburr Twins,
 * Spool Naginata, Beam Staff). Any fighter can take any weapon.
 *
 * A weapon is built in its own frame: the fist at the origin, +y along the weapon. The body
 * gives a grip in that frame (body_grip), so the same weapon sits right in either kind of hand.
 * Each weapon brings its EXTREME (special.h): the web version's ground pound for its four,
 * KEYHOLDER's cinematic specials for its five.
 *
 * To add a weapon: an entry in WEAPONS (weapons.c) with a builder. Keys are on the wire: keep them. */
#ifndef CK_WEAPONS_H
#define CK_WEAPONS_H
#include <stdbool.h>
#include <stdint.h>
#include "three.h"

typedef struct {
  const char *key, *name, *tag, *desc;
  float dmg, spd;          /* damage and attack speed, 1 = the pipe wrench */
  uint32_t trail;          /* the swing trail */
  bool sfx_heavy;          /* the heavy whoosh */
  float seg0, len, r;      /* the striking part: from seg0 to len along +y, radius r (the page's userData) */
  int hands;               /* 2: a copy in the left hand as well */
  int special;             /* SP_* (special.h): what EXTREME does */
  bool web;                /* on the web version's rack: can be used against the web page online */
  t3_object *(*build)(int kh);   /* a new prop group */
  int kh;                  /* KEYHOLDER's prop index for build, -1 for the page's own */
} weapon_def;

extern const weapon_def WEAPONS[];
extern const int WEAPON_N;
int weapon_index(const char *key);          /* -1 if unknown */
t3_object *weapon_build(int w);             /* a new prop */
int weapon_web_count(void);

#endif

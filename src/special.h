/* special.h: EXTREME, the move a full meter buys. Each weapon names one (weapons.h):
 *   SP_EXTREME      the web version's ground pound (the SPECIAL move in fighter.c's table)
 *   SP_FINAL_WELD   KEYHOLDER's Arc Blade: a blink to the foe, three cuts, a cross of light
 *   SP_HAMMERFALL   the Ball Peen Maul: up out of frame and down on them, the floor rings
 *   SP_BURR_CUT     the Deburr Twins: seven cuts from everywhere at once
 *   SP_ADHESION     the Spool Naginata: a turning sweep that draws them in, then the thrust
 *   SP_FULL_POWER   the Beam Staff: a held beam straight through them
 * The KEYHOLDER ones are its fight.c cinematics (cine_update): two seconds with the world held
 * still around the attacker, several blows and the big one last, and a camera of their own.
 * Here they fit the duel: one target, the EXTREME meter pays, the blows land as hits (a KO
 * mid special ends it), and both fighters' clocks are the special's to set.
 *
 * game.c starts one when a fighter asks (fighter.special_req), steps it, takes its camera and
 * its time rates, and applies its blows through the hook. The online guest runs the same
 * timeline for the camera and the effects only (special_follow). */
#ifndef CK_SPECIAL_H
#define CK_SPECIAL_H
#include <stdbool.h>
#include "ck.h"

enum { SP_EXTREME, SP_FINAL_WELD, SP_HAMMERFALL, SP_BURR_CUT, SP_ADHESION, SP_FULL_POWER, SP_N };
const char *special_name(int sp);             /* "GROUND POUND", "FINAL WELD", ... */
bool special_is_cinematic(int sp);

struct fighter;
/* a blow of the special: game.c applies it as a hit (damage, the knockdown on the last) */
extern void (*special_on_blow)(struct fighter *att, struct fighter *def, float dmg, bool last, v3 point, v3 dir);

/* start the attacker's weapon's special against def (NULL or out of reach: it plays and misses) */
void special_start(struct fighter *att, struct fighter *def);
/* the guest's copy, driven by the host's clock: camera and effects, no blows, no moves */
void special_follow(struct fighter *att, struct fighter *def, int sp, float t);
void special_update(float dt);                /* real time */
void special_stop(void);
bool special_active(void);
struct fighter *special_attacker(void);
int special_kind(void);
float special_time(void);
/* while active: how fast the attacker's clock and everyone else's run */
void special_rates(float *attacker, float *world);
/* while active: the cinematic's camera; false to keep the fight camera */
bool special_camera(v3 *pos, v3 *look, float *fov);

#endif

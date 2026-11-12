/* ksound.h: when KEYHOLDER's sound plays in the page (src/kh/ksound.c). On a KEYHOLDER stage its fight music
 * takes over from the page's band and its effects sit on top of the page's; a KEYHOLDER weapon swings with
 * its own sound on any stage. */
#ifndef CK_KSOUND_H
#define CK_KSOUND_H
#include <stdbool.h>
void ksound_round(bool kh_stage, bool final_round);   /* a round starts */
void ksound_match_end(void);                         /* the match is decided */
void ksound_menu(void);                              /* back to the menus: the page's band again */
void ksound_weapon(int kh);                          /* the weapon about to swing: a KEYHOLDER weapon id, or -1 */
void ksound_echo(const char *name, float arg);       /* every page effect passes here (sfx.c tap) */
#endif

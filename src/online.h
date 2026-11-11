/* online.h: online versus and phone pads over the CLASP relay
 * (wss://relay.clasp.to, address space /coffeekill/v1), wire compatible with
 * the page, so the cart and the web version meet in the same rooms.
 *
 * net.c speaks CLASP (binary frames, msgpack values); online.c is the page's
 * Cv (lobby: presence, challenges), qC (a match: snap, intent, ctl), zv (the
 * phone pads) and fv (the online and pads screens). mp.h is the value type. */
#ifndef CK_ONLINE_H
#define CK_ONLINE_H
#include <stdbool.h>
#include "mp.h"

enum { NET_NONE = 0, NET_HOST = 1, NET_GUEST = 2 };

void online_init(void);
void online_update(float dt);

/* fv: the screens' actions */
void online_open(void);
void online_back(void);
void online_join(void);
void online_main_lobby(void);
void online_new_code(void);
void online_copy_link(void);
void online_copy_pad(void);
void online_accept(void);
void online_decline(void);
void online_open_pads(void);
void online_leave_room_status(const char *msg);   /* P.onLeave: back to the lobby with a status line */

/* the match in progress (qC), for the game */
bool online_in_match(void);
int online_role(void);
const char *online_my_name(void);
const char *online_opp_name(void);
void online_snap(mp_val *snap);        /* takes ownership */
void online_intent(mp_val *intent);
void online_ctl(mp_val *ctl);
double online_idle_ms(void);
void online_end_match(void);

/* the game's handlers (game.c) */
void game_begin_online(int role, const char *my_name, const char *my_weapon, const char *opp_name, const char *opp_weapon);
void game_on_snap(const mp_val *snap);
void game_on_intent(const mp_val *intent);
void game_on_ctl(const mp_val *ctl);
void game_leave_online(const char *msg);

#endif

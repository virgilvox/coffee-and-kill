/* net.h: a CLASP client (the page's WC class from @clasp-to/core) over
 * wasmcart's wc_peer: HELLO / WELCOME, SUBSCRIBE with * and ** patterns,
 * SET with a ttl, PUBLISH as event or stream, PING / PONG, reconnects with
 * resubscription. Values are CLASP's typed binary values (numbers float64,
 * strings, bools, null, arrays, maps), carried in mp_val. */
#ifndef CK_NET_H
#define CK_NET_H
#include <stdbool.h>
#include "mp.h"

#define CLASP_RELAY "wss://relay.clasp.to"

enum { NET_OFFLINE, NET_CONNECTING, NET_ONLINE, NET_ERROR };
typedef void (*clasp_cb)(const mp_val *v, const char *addr, void *ctx);

/* connect (a no-op when online or connecting); the status moves to NET_ONLINE
 * on WELCOME, or NET_ERROR after the 9 s the page allows */
void clasp_connect(void);
int clasp_status(void);
void clasp_close(void);
void clasp_update(float dt);
int clasp_sub(const char *pattern, clasp_cb cb, void *ctx);   /* the unsubscribe id */
void clasp_unsub(int id);
void clasp_set(const char *addr, const mp_val *v, double ttl);   /* ttl < 0: none */
void clasp_emit(const char *addr, const mp_val *v);
void clasp_stream(const char *addr, const mp_val *v);
bool clasp_match(const char *pattern, const char *addr);

#endif

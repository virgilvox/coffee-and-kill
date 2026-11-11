/* ck.h: what every COFFEE & KILL source shares. The screen size, the
 * Math.random stand-in, small math, colours and the asset loader.
 *
 * The original is a three.js page (coffee-and-kill.html). Names in comments
 * refer to its minified identifiers where that helps find the matching code
 * (wv is the game, Y8 the fighter, Rr the UI, Z0 the arena, and so on). */
#ifndef CK_COMMON_H
#define CK_COMMON_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>

#define CK_W 1280
#define CK_H 720
#define CK_PI 3.14159265358979323846f

/* Math.random(): a xorshift128+ seeded once at init (wc_set_seed) */
void ck_seed(uint32_t s);
float ck_rand(void);                       /* [0, 1) */
static inline float ck_rr(float a, float b) { return a + ck_rand() * (b - a); }   /* re() / ZP() */

static inline float ck_clamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float ck_lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float ck_sign(float v) { return v > 0 ? 1.f : v < 0 ? -1.f : 0.f; }
static inline float ck_wrap_pi(float a) { return atan2f(sinf(a), cosf(a)); }

/* time since start, seconds (performance.now() / 1000 equivalent) */
double ck_now(void);

/* asset bytes from the cart (wc_load_asset); malloc'd, NULL if missing */
uint8_t *ck_asset(const char *name, size_t *len);

/* the three web fonts of the page, each a single weight (see assets/fonts) */
enum { F_MARKER = 0, F_TYPE = 1, F_LCD = 2 };   /* Archivo Black, IBM Plex Mono SemiBold, JetBrains Mono */

/* palette (:root of the page) */
#define C_PINK   0xf2ab1eu
#define C_CREAM  0xece3d3u
#define C_INK    0x15120fu
#define C_VOID   0x14110du
#define C_PAPER  0xfaf6ecu
#define C_CYAN   0x5e93c4u
#define C_SLIME  0x5fb892u
#define C_ERR    0xd24f3au
#define C_RUST   0xc2542cu

void ck_log(const char *fmt, ...);

#endif

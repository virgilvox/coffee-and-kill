/* gfx.h: the fight's effects: the particle system ($r), the weapon trails
 * (Av), the impact stars (Pv) and the shock rings (ev). Updated by the game,
 * drawn in the fx pass. */
#ifndef CK_GFX_H
#define CK_GFX_H
#include <stdbool.h>
#include <stdint.h>

typedef struct { float x, y, z; } gv3;

void gfx_init(void);
void gfx_set_scale(float viewport_h, float fov_deg);     /* $r.setScale */

/* $r.burst(origin, n, opts) */
typedef struct {
  float speed; bool has_dir; gv3 dir; float spread, life, size; float color[3];
  float gravity, drag, jitter;
} burst_opts;
burst_opts burst_defaults(void);   /* speed 5, spread 1, life .6, size .06, color (1,.7,.2), gravity 9, drag .8, jitter .5 */
void parts_burst(gv3 at, int n, const burst_opts *o);
void parts_update(float dt);

/* Av: one per fighter */
typedef struct trail trail;
trail *trail_new(uint32_t color, int n);
void trail_set_color(trail *t, uint32_t color);
void trail_push(trail *t, gv3 a, gv3 b);
void trail_decay(trail *t);
void trail_clear(trail *t);
void trail_update(trail *t);

/* Pv.spawn and ev.spawn */
void bursts_spawn(gv3 at, float scale, bool blocked);
void bursts_update(float dt);
void rings_spawn(gv3 at, float max, uint32_t color, float dur);
void rings_update(float dt);

/* draw them all (between fx_begin and fx_end), in the page's render order:
 * rings (8), trails (9), particles (10), bursts (20) */
void gfx_draw(trail **trails, int ntrails);
void gfx_clear(void);

#endif

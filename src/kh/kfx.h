/* fx.h: particles, sparks, shockwave rings and additive flashes in the 3D scene. Three
 * instanced pools, so three draw calls at most, no allocation after init. */
#ifndef KH_KFX_H
#define KH_KFX_H
#include "ds.h"
#include "three.h"

void kfx_init(t3_scene *scene);
void kfx_attach(t3_scene *scene);                     /* the pools into another scene (makes them the first time) */
void kfx_clear(void);
void kfx_update(float dt, const t3_camera *cam);        /* after the camera moved */
void kfx_sparks(v3 at, v3 normal, uint32_t color, int count);   /* a hit: a hot spray that bounces */
void kfx_clash(v3 at, uint32_t color);                  /* steel on steel: a white star and a fan of sparks */
void kfx_flash(v3 at, float size, uint32_t color);      /* a round additive flash */
void kfx_shock(v3 at, float r0, float r1, uint32_t color, float life);   /* a ring on the ground growing out */
void kfx_ring_up(v3 at, float r0, float r1, uint32_t color, float life); /* a ring facing the camera growing out */
void kfx_dust(v3 at, float size, int count);            /* feet, falls, landings */
void kfx_motes(v3 at, uint32_t color, int count, float speed);  /* slow glowing motes */
void kfx_streak(v3 a, v3 b, uint32_t color, float width, float life);   /* a bright line that fades */
void kfx_burst(v3 at, uint32_t color, float size);      /* the summon and the big hits: flash, rings, sparks */
/* one frame draws, call every frame while the thing exists */
void kfx_glow(v3 at, float size, uint32_t color);
void kfx_glint(v3 at, float size, uint32_t color);      /* a four point star: a blade about to swing */
void kfx_beam(v3 from, v3 to, uint32_t color, float width);
void kfx_ring_flat(v3 at, float radius, uint32_t color);/* flat on the ground */
void kfx_smoke(v3 at, float size, uint32_t color, int count, float rise);   /* coloured puffs: gas, frost, steam */

/* props: small vertex coloured meshes, PS2 style. Build between kfxp_begin and kfxp_end in the prop's own
 * frame (y up, origin on the floor); kfxp_end adds a group to the scene, at most three draw calls
 * (lit body, unlit glow, additive halo). Remove it with t3_object_remove(&scene->base, obj). */
enum { KFXP_BODY, KFXP_GLOW, KFXP_HALO };
void kfxp_begin(void);
void kfxp_layer(int layer);
void kfxp_box(v3 c, v3 size, uint32_t color);
void kfxp_cyl(v3 base, float h, float r0, float r1, int sides, uint32_t color);
void kfxp_quad(v3 a, v3 b, v3 c, v3 d, uint32_t color);
void kfxp_disc(v3 c, float r, int sides, uint32_t inner, uint32_t outer);     /* flat, facing up */
void kfxp_bar(v3 a, v3 b, float w, uint32_t color);                            /* a square rod from a to b */
void kfxp_ring(v3 c, v3 axis, float R, float r, int segs, uint32_t color);    /* a torus about axis: a wheel */
t3_object *kfxp_end(void);
#endif

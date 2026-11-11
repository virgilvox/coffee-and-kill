/* city.h: downtown Mesa, inside world.c's town stage. See world.h for the public queries. */
#ifndef KH_CITY_H
#define KH_CITY_H
#include "ds.h"
#include "three.h"
void city_init(t3_object *root);
/* cap: the furthest any chunk draws (the lab's storefront only shows so much) */
void city_update(float dt, v3 focus, float cap);
/* night 0..1 (lamps, neon, lit windows); tint multiplies every lit material, per channel */
void city_set_time(float night, t3_color tint);
float city_floor_y(float x, float z, float y);       /* highest ground or prop top at or below y + a step */
void city_push(v3 *p, float r, float h, v3 *hit_normal, bool *hit);
bool city_raycast(v3 o, v3 d, float max_dist, float *t, v3 *n);
bool city_place(const char *name, v3 *pos, float *yaw);
int city_stats(int *tris);
bool city_train_bell(v3 *at);
void city_traffic(bool on);                        /* see world_train_bell */                           /* meshes drawing now, their triangles */
#endif

/* kweapon.h: KEYHOLDER's five weapon props (kweapon.c), built for weapons.c. Weapon frame:
 * the fist at the origin, +y along the blade, +z the cutting edge. */
#ifndef CK_KWEAPON_H
#define CK_KWEAPON_H
#include "ds.h"
#include "three.h"

enum { WP_ARC, WP_MAUL, WP_TWIN, WP_SPEAR, WP_STAFF, WP_COUNT };
t3_object *kweapon_build(int w);                    /* a new group: the lit body, the glow, the additive halo */
uint32_t kweapon_color(int w);                      /* the glow colour */
void kweapon_segment(int w, float *base, float *tip);   /* the blade along +y, metres */
#endif

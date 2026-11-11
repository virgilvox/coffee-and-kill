/* ds.h: KEYHOLDER's shared header, as the modules taken from it see it here (src/kh: the
 * bodies, the world and its stages, the 3D effects). Its names map onto ck.h. */
#ifndef DS_COMMON_H
#define DS_COMMON_H
#include "ck.h"

#define DS_W CK_W
#define DS_H CK_H
#define DS_PI CK_PI

static inline v3 v3_(float x, float y, float z) { v3 r = {x, y, z}; return r; }
static inline v3 v3_add(v3 a, v3 b) { return v3_(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline v3 v3_sub(v3 a, v3 b) { return v3_(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline v3 v3_scale(v3 a, float s) { return v3_(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float v3_len(v3 a) { return sqrtf(v3_dot(a, a)); }
static inline v3 v3_norm(v3 a) { float l = v3_len(a); return l > 1e-6f ? v3_scale(a, 1 / l) : v3_(0, 0, 0); }
static inline v3 v3_lerp(v3 a, v3 b, float t) { return v3_(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
static inline v3 v3_cross(v3 a, v3 b) { return v3_(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

#define ds_clamp ck_clamp
#define ds_lerp ck_lerp
#define ds_sign ck_sign
#define ds_wrap_pi ck_wrap_pi
static inline float ds_damp(float cur, float target, float speed, float dt) { return ck_lerp(cur, target, 1 - expf(-speed * dt)); }
#define ds_rand ck_rand
#define ds_rr ck_rr
#define ds_now ck_now
#define ds_asset ck_asset
#define ds_log ck_log

/* KEYHOLDER's palette, for the parts that use it */
#define C_HIVIS  0xff7a1au
#define C_TEAL   0x2ec4b6u
#define C_SKY    0x1c2030u
#define C_DUSK   0x23263au
#define C_ALERT  0xe8433au
#endif

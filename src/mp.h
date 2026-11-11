/* mp.h: a small JSON-like value (null, bool, number, string, array, map)
 * with msgpack encoding and decoding, for the CLASP payloads the page sends
 * (@msgpack/msgpack: integers as the smallest int, other numbers float64,
 * strings str8/16/32, arrays, maps with string keys). */
#ifndef CK_MP_H
#define CK_MP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { MP_NIL, MP_BOOL, MP_NUM, MP_STR, MP_ARR, MP_MAP, MP_BIN } mp_type;
typedef struct mp_val mp_val;
struct mp_val {
  mp_type t;
  bool b; double n; bool is_int;
  char *s; size_t slen;            /* MP_STR, MP_BIN */
  mp_val **items; char **keys; int count, cap;   /* MP_ARR (keys NULL), MP_MAP */
};

mp_val *mp_nil(void);
mp_val *mp_bool(bool b);
mp_val *mp_num(double n);          /* encodes as an int when integral, like the JS encoder */
mp_val *mp_str(const char *s);
mp_val *mp_arr(void);
mp_val *mp_map(void);
mp_val *mp_push(mp_val *arr, mp_val *v);            /* returns arr */
mp_val *mp_set(mp_val *map, const char *k, mp_val *v);   /* returns map (replaces) */
void mp_free(mp_val *v);
mp_val *mp_clone(const mp_val *v);

const mp_val *mp_get(const mp_val *map, const char *k);
const mp_val *mp_at(const mp_val *arr, int i);
double mp_getn(const mp_val *map, const char *k, double def);
const char *mp_gets(const mp_val *map, const char *k, const char *def);
bool mp_truthy(const mp_val *v);

/* msgpack */
size_t mp_encode(const mp_val *v, uint8_t *out, size_t cap);   /* bytes written, 0 if it does not fit */
mp_val *mp_decode(const uint8_t *p, size_t n, size_t *used);

#endif

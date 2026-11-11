/* mp.c: see mp.h. Encoding follows @msgpack/msgpack's defaults (what the
 * page's CLASP client uses): integers in the smallest form, other numbers as
 * float64, strings as str, arrays, maps. */
#include "mp.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

static mp_val *nv(mp_type t) { mp_val *v = calloc(1, sizeof *v); v->t = t; return v; }
mp_val *mp_nil(void) { return nv(MP_NIL); }
mp_val *mp_bool(bool b) { mp_val *v = nv(MP_BOOL); v->b = b; return v; }
mp_val *mp_num(double n) {
  mp_val *v = nv(MP_NUM); v->n = n;
  v->is_int = isfinite(n) && floor(n) == n && fabs(n) < 9007199254740992.0;
  return v;
}
mp_val *mp_str(const char *s) {
  mp_val *v = nv(MP_STR);
  if (!s) s = "";
  v->slen = strlen(s); v->s = malloc(v->slen + 1); memcpy(v->s, s, v->slen + 1);
  return v;
}
mp_val *mp_arr(void) { return nv(MP_ARR); }
mp_val *mp_map(void) { return nv(MP_MAP); }
static void grow(mp_val *c) {
  if (c->count < c->cap) return;
  c->cap = c->cap ? c->cap * 2 : 4;
  c->items = realloc(c->items, sizeof(mp_val *) * c->cap);
  if (c->t == MP_MAP) c->keys = realloc(c->keys, sizeof(char *) * c->cap);
}
mp_val *mp_push(mp_val *a, mp_val *v) { if (!a) { mp_free(v); return a; } grow(a); a->items[a->count++] = v; return a; }
mp_val *mp_set(mp_val *m, const char *k, mp_val *v) {
  if (!m) { mp_free(v); return m; }
  for (int i = 0; i < m->count; i++) if (!strcmp(m->keys[i], k)) { mp_free(m->items[i]); m->items[i] = v; return m; }
  grow(m);
  size_t n = strlen(k);
  m->keys[m->count] = malloc(n + 1); memcpy(m->keys[m->count], k, n + 1);
  m->items[m->count++] = v;
  return m;
}
void mp_free(mp_val *v) {
  if (!v) return;
  for (int i = 0; i < v->count; i++) { mp_free(v->items[i]); if (v->keys) free(v->keys[i]); }
  free(v->items); free(v->keys); free(v->s); free(v);
}
mp_val *mp_clone(const mp_val *v) {
  if (!v) return NULL;
  mp_val *c = nv(v->t);
  c->b = v->b; c->n = v->n; c->is_int = v->is_int;
  if (v->s) { c->slen = v->slen; c->s = malloc(v->slen + 1); memcpy(c->s, v->s, v->slen); c->s[v->slen] = 0; }
  for (int i = 0; i < v->count; i++) {
    if (v->t == MP_MAP) mp_set(c, v->keys[i], mp_clone(v->items[i]));
    else mp_push(c, mp_clone(v->items[i]));
  }
  return c;
}
const mp_val *mp_get(const mp_val *m, const char *k) {
  if (!m || m->t != MP_MAP) return NULL;
  for (int i = 0; i < m->count; i++) if (!strcmp(m->keys[i], k)) return m->items[i];
  return NULL;
}
const mp_val *mp_at(const mp_val *a, int i) { return a && a->t == MP_ARR && i >= 0 && i < a->count ? a->items[i] : NULL; }
double mp_getn(const mp_val *m, const char *k, double d) { const mp_val *v = mp_get(m, k); return v && v->t == MP_NUM ? v->n : v && v->t == MP_BOOL ? v->b : d; }
const char *mp_gets(const mp_val *m, const char *k, const char *d) { const mp_val *v = mp_get(m, k); return v && v->t == MP_STR ? v->s : d; }
bool mp_truthy(const mp_val *v) {
  if (!v) return false;
  switch (v->t) {
    case MP_NIL: return false;
    case MP_BOOL: return v->b;
    case MP_NUM: return v->n != 0 && !isnan(v->n);
    case MP_STR: return v->slen > 0;
    default: return true;
  }
}

/* ---- encode ---- */
typedef struct { uint8_t *p; size_t n, cap; bool bad; } wbuf;
static void put(wbuf *w, const void *d, size_t n) { if (w->n + n > w->cap) { w->bad = true; return; } memcpy(w->p + w->n, d, n); w->n += n; }
static void u8(wbuf *w, uint8_t v) { put(w, &v, 1); }
static void be(wbuf *w, uint64_t v, int bytes) { uint8_t b[8]; for (int i = 0; i < bytes; i++) b[i] = (uint8_t)(v >> (8 * (bytes - 1 - i))); put(w, b, bytes); }
static void enc_str(wbuf *w, const char *s, size_t n) {
  if (n < 32) u8(w, (uint8_t)(0xa0 | n));
  else if (n < 256) { u8(w, 0xd9); u8(w, (uint8_t)n); }
  else if (n < 65536) { u8(w, 0xda); be(w, n, 2); }
  else { u8(w, 0xdb); be(w, n, 4); }
  put(w, s, n);
}
static void enc(wbuf *w, const mp_val *v) {
  if (!v) { u8(w, 0xc0); return; }
  switch (v->t) {
    case MP_NIL: u8(w, 0xc0); break;
    case MP_BOOL: u8(w, v->b ? 0xc3 : 0xc2); break;
    case MP_NUM:
      if (v->is_int) {
        double d = v->n;
        if (d >= 0) {
          uint64_t u = (uint64_t)d;
          if (u < 128) u8(w, (uint8_t)u);
          else if (u < 256) { u8(w, 0xcc); u8(w, (uint8_t)u); }
          else if (u < 65536) { u8(w, 0xcd); be(w, u, 2); }
          else if (u <= 0xffffffffull) { u8(w, 0xce); be(w, u, 4); }
          else { u8(w, 0xcf); be(w, u, 8); }
        } else {
          int64_t i = (int64_t)d;
          if (i >= -32) u8(w, (uint8_t)(int8_t)i);
          else if (i >= -128) { u8(w, 0xd0); u8(w, (uint8_t)(int8_t)i); }
          else if (i >= -32768) { u8(w, 0xd1); be(w, (uint16_t)(int16_t)i, 2); }
          else if (i >= -2147483648ll) { u8(w, 0xd2); be(w, (uint32_t)(int32_t)i, 4); }
          else { u8(w, 0xd3); be(w, (uint64_t)i, 8); }
        }
      } else {
        uint64_t b; double d = v->n; memcpy(&b, &d, 8);
        u8(w, 0xcb); be(w, b, 8);
      }
      break;
    case MP_STR: enc_str(w, v->s, v->slen); break;
    case MP_BIN:
      if (v->slen < 256) { u8(w, 0xc4); u8(w, (uint8_t)v->slen); }
      else if (v->slen < 65536) { u8(w, 0xc5); be(w, v->slen, 2); }
      else { u8(w, 0xc6); be(w, v->slen, 4); }
      put(w, v->s, v->slen);
      break;
    case MP_ARR:
      if (v->count < 16) u8(w, (uint8_t)(0x90 | v->count));
      else if (v->count < 65536) { u8(w, 0xdc); be(w, v->count, 2); }
      else { u8(w, 0xdd); be(w, v->count, 4); }
      for (int i = 0; i < v->count; i++) enc(w, v->items[i]);
      break;
    case MP_MAP:
      if (v->count < 16) u8(w, (uint8_t)(0x80 | v->count));
      else if (v->count < 65536) { u8(w, 0xde); be(w, v->count, 2); }
      else { u8(w, 0xdf); be(w, v->count, 4); }
      for (int i = 0; i < v->count; i++) { enc_str(w, v->keys[i], strlen(v->keys[i])); enc(w, v->items[i]); }
      break;
  }
}
size_t mp_encode(const mp_val *v, uint8_t *out, size_t cap) {
  wbuf w = {out, 0, cap, false};
  enc(&w, v);
  return w.bad ? 0 : w.n;
}

/* ---- decode ---- */
typedef struct { const uint8_t *p; size_t n, i; bool bad; } rbuf;
static uint64_t rd(rbuf *r, int bytes) {
  if (r->i + bytes > r->n) { r->bad = true; return 0; }
  uint64_t v = 0; for (int k = 0; k < bytes; k++) v = (v << 8) | r->p[r->i++];
  return v;
}
static mp_val *dec(rbuf *r, int depth);
static mp_val *dec_str(rbuf *r, size_t n, mp_type t) {
  if (r->i + n > r->n) { r->bad = true; return mp_nil(); }
  mp_val *v = nv(t); v->slen = n; v->s = malloc(n + 1); memcpy(v->s, r->p + r->i, n); v->s[n] = 0; r->i += n;
  return v;
}
static mp_val *dec_arr(rbuf *r, size_t n, int depth) {
  mp_val *a = mp_arr();
  for (size_t k = 0; k < n && !r->bad; k++) mp_push(a, dec(r, depth + 1));
  return a;
}
static mp_val *dec_map(rbuf *r, size_t n, int depth) {
  mp_val *m = mp_map();
  for (size_t k = 0; k < n && !r->bad; k++) {
    mp_val *key = dec(r, depth + 1), *val = dec(r, depth + 1);
    char kb[32]; const char *ks = kb;
    if (key->t == MP_STR) ks = key->s;
    else if (key->t == MP_NUM) { snprintf(kb, sizeof kb, "%g", key->n); }
    else kb[0] = 0;
    mp_set(m, ks, val); mp_free(key);
  }
  return m;
}
static mp_val *dec(rbuf *r, int depth) {
  if (depth > 32 || r->i >= r->n) { r->bad = true; return mp_nil(); }
  uint8_t b = r->p[r->i++];
  if (b < 0x80) return mp_num(b);
  if (b >= 0xe0) return mp_num((int8_t)b);
  if ((b & 0xf0) == 0x80) return dec_map(r, b & 15, depth);
  if ((b & 0xf0) == 0x90) return dec_arr(r, b & 15, depth);
  if ((b & 0xe0) == 0xa0) return dec_str(r, b & 31, MP_STR);
  switch (b) {
    case 0xc0: return mp_nil();
    case 0xc2: return mp_bool(false);
    case 0xc3: return mp_bool(true);
    case 0xc4: return dec_str(r, rd(r, 1), MP_BIN);
    case 0xc5: return dec_str(r, rd(r, 2), MP_BIN);
    case 0xc6: return dec_str(r, rd(r, 4), MP_BIN);
    case 0xca: { uint32_t u = (uint32_t)rd(r, 4); float f; memcpy(&f, &u, 4); return mp_num(f); }
    case 0xcb: { uint64_t u = rd(r, 8); double d; memcpy(&d, &u, 8); return mp_num(d); }
    case 0xcc: return mp_num((double)rd(r, 1));
    case 0xcd: return mp_num((double)rd(r, 2));
    case 0xce: return mp_num((double)rd(r, 4));
    case 0xcf: return mp_num((double)rd(r, 8));
    case 0xd0: return mp_num((int8_t)rd(r, 1));
    case 0xd1: return mp_num((int16_t)rd(r, 2));
    case 0xd2: return mp_num((int32_t)rd(r, 4));
    case 0xd3: return mp_num((double)(int64_t)rd(r, 8));
    case 0xd9: return dec_str(r, rd(r, 1), MP_STR);
    case 0xda: return dec_str(r, rd(r, 2), MP_STR);
    case 0xdb: return dec_str(r, rd(r, 4), MP_STR);
    case 0xdc: return dec_arr(r, rd(r, 2), depth);
    case 0xdd: return dec_arr(r, rd(r, 4), depth);
    case 0xde: return dec_map(r, rd(r, 2), depth);
    case 0xdf: return dec_map(r, rd(r, 4), depth);
    case 0xd4: rd(r, 2); return mp_nil();   /* fixext: skipped (the timestamp extension the page never sends) */
    case 0xd5: rd(r, 3); return mp_nil();
    case 0xd6: rd(r, 5); return mp_nil();
    case 0xd7: rd(r, 9); return mp_nil();
    case 0xd8: rd(r, 17); return mp_nil();
    default: r->bad = true; return mp_nil();
  }
}
mp_val *mp_decode(const uint8_t *p, size_t n, size_t *used) {
  rbuf r = {p, n, 0, false};
  mp_val *v = dec(&r, 0);
  if (used) *used = r.i;
  if (r.bad) { mp_free(v); return NULL; }
  return v;
}

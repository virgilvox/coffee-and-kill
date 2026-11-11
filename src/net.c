/* net.c: see net.h. Frames: 'S', flags (qos << 6 | timestamp 0x20 | version
 * 1), u16 payload length, payload. Payloads follow the page's encoder (vI)
 * and decoder (oI) for the messages the game uses. */
#include "net.h"
#include "wasmcart.h"
#include "ck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { M_HELLO = 1, M_WELCOME = 2, M_SUBSCRIBE = 16, M_UNSUBSCRIBE = 17, M_PUBLISH = 32, M_SET = 33,
       M_SNAPSHOT = 35, M_BUNDLE = 48, M_PING = 65, M_PONG = 66, M_ERROR = 81 };
enum { V_NULL, V_BOOL, V_I8, V_I16, V_I32, V_I64, V_F32, V_F64, V_STRING, V_BYTES, V_ARRAY, V_MAP };

#define MAXSUB 32
static struct { bool used; int id; char pattern[96]; clasp_cb cb; void *ctx; } subs[MAXSUB];
static int next_sub = 1;
static int sock = -1, status = NET_OFFLINE;
static float since_dial, retry_in = -1;
static int reconnects;
static bool closing;
static double server_time_us, welcome_at;

int clasp_status(void) { return status; }

bool clasp_match(const char *p, const char *a) {
  /* iI: ** is any run of characters, * one or more non-slash characters */
  if (!*p) return !*a;
  if (p[0] == '*' && p[1] == '*') {
    for (const char *t = a;; t++) { if (clasp_match(p + 2, t)) return true; if (!*t) return false; }
  }
  if (p[0] == '*') {
    if (!*a || *a == '/') return false;
    for (const char *t = a + 1;; t++) {
      if (clasp_match(p + 1, t)) return true;
      if (!*t || *t == '/') return false;
    }
  }
  return *p == *a && clasp_match(p + 1, a + 1);
}

/* ---- writing ---- */
static uint8_t buf[65536 + 16];
typedef struct { uint8_t *p; size_t n, cap; bool bad; } wb;
static void w8(wb *w, uint8_t v) { if (w->n + 1 > w->cap) { w->bad = true; return; } w->p[w->n++] = v; }
static void wbe(wb *w, uint64_t v, int k) { for (int i = k - 1; i >= 0; i--) w8(w, (uint8_t)(v >> (8 * i))); }
static void wstr(wb *w, const char *s, size_t n) {
  if (n > 65535) n = 65535;
  wbe(w, n, 2);
  if (w->n + n > w->cap) { w->bad = true; return; }
  memcpy(w->p + w->n, s, n); w->n += n;
}
static int vtype(const mp_val *v) {
  if (!v) return V_NULL;
  switch (v->t) {
    case MP_BOOL: return V_BOOL;
    case MP_NUM: return V_F64;
    case MP_STR: return V_STRING;
    case MP_BIN: return V_BYTES;
    case MP_ARR: return V_ARRAY;
    case MP_MAP: return V_MAP;
    default: return V_NULL;
  }
}
static void wval(wb *w, const mp_val *v) {   /* kD */
  if (!v) return;
  switch (v->t) {
    case MP_NIL: break;
    case MP_BOOL: w8(w, v->b ? 1 : 0); break;
    case MP_NUM: { uint64_t b; memcpy(&b, &v->n, 8); wbe(w, b, 8); break; }
    case MP_STR: case MP_BIN: wstr(w, v->s, v->slen); break;
    case MP_ARR:
      wbe(w, v->count, 2);
      for (int i = 0; i < v->count; i++) { w8(w, (uint8_t)vtype(v->items[i])); wval(w, v->items[i]); }
      break;
    case MP_MAP:
      wbe(w, v->count, 2);
      for (int i = 0; i < v->count; i++) {
        wstr(w, v->keys[i], strlen(v->keys[i]));
        w8(w, (uint8_t)vtype(v->items[i])); wval(w, v->items[i]);
      }
      break;
  }
}
static double now_us(void) { return server_time_us + (ck_now() - welcome_at) * 1e6; }

static void send_frame(wb *w, int qos) {
  if (sock < 0 || w->bad) return;
  size_t len = w->n - 4;
  buf[0] = 0x53; buf[1] = (uint8_t)((qos & 3) << 6 | 1);
  buf[2] = (uint8_t)(len >> 8); buf[3] = (uint8_t)len;
  wc_peer_send(sock, buf, (unsigned)w->n);
}
static wb begin(void) { wb w = {buf, 4, sizeof buf - 16, false}; return w; }

static void send_hello(void) {
  wb w = begin();
  w8(&w, M_HELLO); w8(&w, 1); w8(&w, 128 | 64 | 32);   /* version 1; param, event, stream */
  wstr(&w, "coffee-and-kill", 15);
  wbe(&w, 0, 2);
  send_frame(&w, 0);
}
static void send_subscribe(int id, const char *pattern) {
  wb w = begin();
  w8(&w, M_SUBSCRIBE); wbe(&w, (uint32_t)id, 4); wstr(&w, pattern, strlen(pattern));
  w8(&w, 255); w8(&w, 0);
  send_frame(&w, 1);
}

void clasp_set(const char *addr, const mp_val *v, double ttl) {
  if (status != NET_ONLINE) return;
  wb w = begin();
  w8(&w, M_SET);
  uint8_t f = (uint8_t)(vtype(v) & 15); if (ttl >= 0) f |= 16;
  w8(&w, f);
  wstr(&w, addr, strlen(addr));
  wval(&w, v);
  if (ttl >= 0) wbe(&w, ttl == 0 ? 0 : ((uint32_t)ttl & 0x7fffffffu), 4);
  send_frame(&w, 1);
}
static void publish(const char *addr, const mp_val *v, int signal, int qos) {
  if (status != NET_ONLINE) return;
  wb w = begin();
  w8(&w, M_PUBLISH);
  w8(&w, (uint8_t)((signal & 7) << 5 | 16));         /* signal, a timestamp, phase start */
  wstr(&w, addr, strlen(addr));
  w8(&w, 1); w8(&w, (uint8_t)vtype(v)); wval(&w, v);
  wbe(&w, (uint64_t)now_us(), 8);
  send_frame(&w, qos);
}
void clasp_emit(const char *addr, const mp_val *v) { publish(addr, v, 1, 1); }
void clasp_stream(const char *addr, const mp_val *v) { publish(addr, v, 2, 0); }

int clasp_sub(const char *pattern, clasp_cb cb, void *ctx) {
  for (int i = 0; i < MAXSUB; i++) if (!subs[i].used) {
    subs[i].used = true; subs[i].id = next_sub++; subs[i].cb = cb; subs[i].ctx = ctx;
    snprintf(subs[i].pattern, sizeof subs[i].pattern, "%s", pattern);
    if (status == NET_ONLINE) send_subscribe(subs[i].id, pattern);
    return subs[i].id;
  }
  return 0;
}
void clasp_unsub(int id) {
  for (int i = 0; i < MAXSUB; i++) if (subs[i].used && subs[i].id == id) {
    subs[i].used = false;
    if (status == NET_ONLINE) { wb w = begin(); w8(&w, M_UNSUBSCRIBE); wbe(&w, (uint32_t)id, 4); send_frame(&w, 1); }
  }
}

/* ---- reading ---- */
typedef struct { const uint8_t *p; size_t n, i; bool bad; } rb;
static uint64_t rbe(rb *r, int k) { if (r->i + k > r->n) { r->bad = true; return 0; } uint64_t v = 0; for (int i = 0; i < k; i++) v = v << 8 | r->p[r->i++]; return v; }
static char *rstr(rb *r, size_t *len) {
  size_t n = (size_t)rbe(r, 2);
  if (r->bad || r->i + n > r->n) { r->bad = true; if (len) *len = 0; return calloc(1, 1); }
  char *s = malloc(n + 1); memcpy(s, r->p + r->i, n); s[n] = 0; r->i += n;
  if (len) *len = n;
  return s;
}
static mp_val *rval(rb *r, int t, int depth) {   /* Ag */
  if (depth > 24) { r->bad = true; return mp_nil(); }
  switch (t) {
    case V_NULL: return mp_nil();
    case V_BOOL: return mp_bool(rbe(r, 1) != 0);
    case V_I8: return mp_num((int8_t)rbe(r, 1));
    case V_I16: return mp_num((int16_t)rbe(r, 2));
    case V_I32: return mp_num((int32_t)rbe(r, 4));
    case V_I64: return mp_num((double)(int64_t)rbe(r, 8));
    case V_F32: { uint32_t u = (uint32_t)rbe(r, 4); float f; memcpy(&f, &u, 4); return mp_num(f); }
    case V_F64: { uint64_t u = rbe(r, 8); double d; memcpy(&d, &u, 8); return mp_num(d); }
    case V_STRING: { size_t n; char *s = rstr(r, &n); mp_val *v = mp_str(s); free(s); return v; }
    case V_BYTES: { size_t n; char *s = rstr(r, &n); mp_val *v = mp_str(""); free(v->s); v->t = MP_BIN; v->s = s; v->slen = n; return v; }
    case V_ARRAY: {
      int n = (int)rbe(r, 2); mp_val *a = mp_arr();
      for (int i = 0; i < n && !r->bad; i++) { int tt = (int)rbe(r, 1); mp_push(a, rval(r, tt, depth + 1)); }
      return a;
    }
    case V_MAP: {
      int n = (int)rbe(r, 2); mp_val *m = mp_map();
      for (int i = 0; i < n && !r->bad; i++) {
        char *k = rstr(r, NULL); int tt = (int)rbe(r, 1);
        mp_set(m, k, rval(r, tt, depth + 1)); free(k);
      }
      return m;
    }
    default: r->bad = true; return mp_nil();
  }
}

static void notify(const char *addr, const mp_val *v) {
  for (int i = 0; i < MAXSUB; i++) if (subs[i].used && clasp_match(subs[i].pattern, addr)) {
    int id = subs[i].id;
    subs[i].cb(v, addr, subs[i].ctx);
    if (!subs[i].used || subs[i].id != id) continue;   /* the callback unsubscribed */
  }
}

static void on_payload(const uint8_t *p, size_t n);
static void on_payload(const uint8_t *p, size_t n) {
  if (n < 1) return;
  if ((p[0] & 0xf0) == 0x80 || p[0] == 0xde || p[0] == 0xdf) return;   /* the legacy msgpack framing: not sent by the relay today */
  rb r = {p, n, 1, false};
  switch (p[0]) {
    case M_WELCOME: {
      rbe(&r, 1); rbe(&r, 1);
      server_time_us = (double)rbe(&r, 8); welcome_at = ck_now();
      status = NET_ONLINE; reconnects = 0;
      for (int i = 0; i < MAXSUB; i++) if (subs[i].used) send_subscribe(subs[i].id, subs[i].pattern);
      WC_LOG("net: relay welcomed us");
      break;
    }
    case M_SET: {
      int f = (int)rbe(&r, 1);
      char *addr = rstr(&r, NULL);
      mp_val *v = rval(&r, f & 15, 0);
      if (!r.bad) notify(addr, v);
      mp_free(v); free(addr);
      break;
    }
    case M_SNAPSHOT: {
      int cnt = (int)rbe(&r, 2);
      for (int k = 0; k < cnt && !r.bad; k++) {
        char *addr = rstr(&r, NULL);
        int t = (int)rbe(&r, 1);
        mp_val *v = rval(&r, t, 0);
        rbe(&r, 8);
        int fl = (int)rbe(&r, 1);
        if (fl & 1) free(rstr(&r, NULL));
        if (fl & 2) rbe(&r, 8);
        if (!r.bad) notify(addr, v);
        mp_free(v); free(addr);
      }
      break;
    }
    case M_PUBLISH: {
      int f = (int)rbe(&r, 1);
      char *addr = rstr(&r, NULL);
      int ind = (int)rbe(&r, 1);
      mp_val *v = NULL;
      if (ind == 1) { int t = (int)rbe(&r, 1); v = rval(&r, t, 0); }
      (void)f;
      if (!r.bad) notify(addr, v ? v : NULL);
      mp_free(v); free(addr);
      break;
    }
    case M_BUNDLE: {
      int fl = (int)rbe(&r, 1), cnt = (int)rbe(&r, 2);
      if (fl & 128) rbe(&r, 8);
      for (int k = 0; k < cnt && !r.bad; k++) {
        size_t len = (size_t)rbe(&r, 2);
        if (r.i + len > r.n) break;
        on_payload(r.p + r.i, len);
        r.i += len;
      }
      break;
    }
    case M_PING: { wb w = begin(); w8(&w, M_PONG); send_frame(&w, 0); break; }
    case M_ERROR: {
      rbe(&r, 2); char *m = rstr(&r, NULL);
      ck_log("net: relay error %s", m); free(m);
      break;
    }
    default: break;
  }
}

static void dial(void) {
  sock = wc_peer_open(CLASP_RELAY, (unsigned)strlen(CLASP_RELAY));
  since_dial = 0;
  status = sock >= 0 ? NET_CONNECTING : NET_ERROR;
  ck_log("net: dial %s -> %d", CLASP_RELAY, sock);
}
void clasp_connect(void) {
  closing = false;
  if (status == NET_ONLINE || status == NET_CONNECTING) return;
  dial();
}
void clasp_close(void) {
  closing = true;
  if (sock >= 0) wc_peer_close(sock);
  sock = -1; status = NET_OFFLINE; retry_in = -1;
}
static void lost(int32_t id) {
  if (id != sock) return;
  sock = -1;
  bool was_online = status == NET_ONLINE;
  status = was_online ? NET_CONNECTING : NET_ERROR;
  /* reconnect: 5 s, times 1.5 per try, up to 30 s, ten tries (scheduleReconnect) */
  if (!closing && reconnects < 10) { float s = 5; for (int i = 0; i < reconnects; i++) s *= 1.5f; retry_in = s > 30 ? 30 : s; reconnects++; }
}
void clasp_update(float dt) {
  if (sock >= 0 && status == NET_CONNECTING && (since_dial += dt) > 9) { int32_t s = sock; wc_peer_close(s); lost(s); status = NET_ERROR; }
  if (sock < 0 && retry_in > 0) { retry_in -= dt; if (retry_in <= 0) { retry_in = -1; dial(); } }
}

__attribute__((export_name("wc_peer_on_connect")))
void wc_peer_on_connect(int32_t id, const char *name, uint32_t name_len) {
  (void)name; (void)name_len;
  if (id == sock) send_hello();
}
__attribute__((export_name("wc_peer_on_message")))
void wc_peer_on_message(int32_t id, const uint8_t *data, uint32_t len) {
  if (id != sock) return;
  uint32_t off = 0;
  while (off + 4 <= len) {
    if (data[off] != 0x53) return;
    int flags = data[off + 1];
    uint32_t plen = (uint32_t)(data[off + 2] << 8 | data[off + 3]);
    uint32_t hdr = (flags & 0x20) ? 12 : 4;
    if (off + hdr + plen > len) return;
    on_payload(data + off + hdr, plen);
    off += hdr + plen;
  }
}
__attribute__((export_name("wc_peer_on_disconnect")))
void wc_peer_on_disconnect(int32_t id) { lost(id); }
__attribute__((export_name("wc_peer_on_error")))
void wc_peer_on_error(int32_t id) { lost(id); }

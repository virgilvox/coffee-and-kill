/* ck.c: the shared helpers of ck.h. */
#include "ck.h"
#include "wasmcart.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rs0 = 0x9E3779B97F4A7C15ull, rs1 = 0xBF58476D1CE4E5B9ull;
void ck_seed(uint32_t s) {
  uint64_t z = (uint64_t)s * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
  for (int i = 0; i < 2; i++) {
    z += 0x9E3779B97F4A7C15ull;
    uint64_t x = z;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    if (i == 0) rs0 = x; else rs1 = x;
  }
  if (!rs0 && !rs1) rs1 = 1;
}
/* xorshift128+, the generator V8 uses for Math.random, to 53 bits */
float ck_rand(void) {
  uint64_t s1 = rs0, s0 = rs1;
  rs0 = s0;
  s1 ^= s1 << 23; s1 ^= s1 >> 17; s1 ^= s0; s1 ^= s0 >> 26;
  rs1 = s1;
  double v = (double)((rs0 + rs1) >> 11) / 9007199254740992.0;
  float f = (float)v;
  return f >= 1.f ? 0.99999994f : f;
}

extern double ck_clock_s;   /* main.c: seconds of host time since start */
double ck_now(void) { return ck_clock_s; }

uint8_t *ck_asset(const char *name, size_t *len) {
  int n = wc_asset_size(name, (unsigned)strlen(name));
  if (n < 0) { ck_log("missing asset %s", name); if (len) *len = 0; return NULL; }
  uint8_t *p = malloc((size_t)n + 1);
  if (!p) return NULL;
  int got = wc_load_asset(name, (unsigned)strlen(name), p, (unsigned)n);
  if (got < 0) { free(p); if (len) *len = 0; return NULL; }
  p[got] = 0;
  if (len) *len = (size_t)got;
  return p;
}

void ck_log(const char *fmt, ...) {
  char b[1024];
  va_list ap; va_start(ap, fmt);
  int n = vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n >= (int)sizeof b) n = sizeof b - 1;
  wc_log(b, (unsigned)n);
}

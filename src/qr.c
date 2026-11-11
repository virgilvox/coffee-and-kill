/* qr.c: the page's QR code (Kazuhiko Arase's qrcode-generator, cI(0, "M"):
 * the smallest version that fits, error correction M, 8 bit mode, the mask
 * with the lowest lost point as that library scores it), so the cart's pad
 * screen shows the same modules for the same link. */
#include "qr.h"
#include <string.h>
#include <stdbool.h>

static int EXP[256], LOG[256];
static bool tables;
static void init_tables(void) {
  if (tables) return;
  for (int i = 0; i < 8; i++) EXP[i] = 1 << i;
  for (int i = 8; i < 256; i++) EXP[i] = EXP[i - 4] ^ EXP[i - 5] ^ EXP[i - 6] ^ EXP[i - 8];
  for (int i = 0; i < 255; i++) LOG[EXP[i]] = i;
  tables = true;
}
static int glog(int n) { return LOG[n]; }
static int gexp(int n) { while (n < 0) n += 255; while (n >= 256) n -= 255; return EXP[n]; }

/* RS blocks for level M, versions 1..15: count, total, data (twice when mixed) */
static const int RS_M[15][6] = {
  {1, 26, 16}, {1, 44, 28}, {1, 70, 44}, {2, 50, 32}, {2, 67, 43}, {4, 43, 27}, {4, 49, 31},
  {2, 60, 38, 2, 61, 39}, {3, 58, 36, 2, 59, 37}, {4, 69, 43, 1, 70, 44}, {1, 80, 50, 4, 81, 51},
  {6, 58, 36, 2, 59, 37}, {8, 59, 37, 1, 60, 38}, {4, 64, 40, 5, 65, 41}, {5, 65, 41, 5, 66, 42}};
static const int POS[15][4] = {{0}, {6, 18}, {6, 22}, {6, 26}, {6, 30}, {6, 34}, {6, 22, 38}, {6, 24, 42},
  {6, 26, 46}, {6, 28, 50}, {6, 30, 54}, {6, 32, 58}, {6, 34, 62}, {6, 26, 46, 66}, {6, 26, 48, 70}};
static int npos(int v) { int n = 0; while (n < 4 && POS[v - 1][n]) n++; return n; }

typedef struct { int total, data; } block;
static int blocks(int v, block *out) {
  const int *t = RS_M[v - 1]; int n = 0;
  for (int k = 0; k < 2; k++) { int c = t[k * 3]; for (int i = 0; i < c; i++) { out[n].total = t[k * 3 + 1]; out[n].data = t[k * 3 + 2]; n++; } }
  return n;
}

static int8_t M[QR_MAX * QR_MAX]; static int N;   /* -1 null, 0 light, 1 dark */
#define AT(r, c) M[(r) * N + (c)]

static void probe(int row, int col) {
  for (int r = -1; r <= 7; r++) {
    if (row + r <= -1 || N <= row + r) continue;
    for (int c = -1; c <= 7; c++) {
      if (col + c <= -1 || N <= col + c) continue;
      bool d = (0 <= r && r <= 6 && (c == 0 || c == 6)) || (0 <= c && c <= 6 && (r == 0 || r == 6)) || (2 <= r && r <= 4 && 2 <= c && c <= 4);
      AT(row + r, col + c) = d;
    }
  }
}
static int bch_digit(unsigned d) { int n = 0; while (d) { n++; d >>= 1; } return n; }
static unsigned bch_type_info(unsigned data) {
  const unsigned G15 = 0x537, MASK = 0x5412;
  unsigned d = data << 10;
  while (bch_digit(d) - bch_digit(G15) >= 0) d ^= G15 << (bch_digit(d) - bch_digit(G15));
  return ((data << 10) | d) ^ MASK;
}
static unsigned bch_type_number(unsigned data) {
  const unsigned G18 = 0x1f25;
  unsigned d = data << 12;
  while (bch_digit(d) - bch_digit(G18) >= 0) d ^= G18 << (bch_digit(d) - bch_digit(G18));
  return (data << 12) | d;
}
static bool mask(int p, int i, int j) {
  switch (p) {
    case 0: return (i + j) % 2 == 0;
    case 1: return i % 2 == 0;
    case 2: return j % 3 == 0;
    case 3: return (i + j) % 3 == 0;
    case 4: return (i / 2 + j / 3) % 2 == 0;
    case 5: return (i * j) % 2 + (i * j) % 3 == 0;
    case 6: return ((i * j) % 2 + (i * j) % 3) % 2 == 0;
    default: return ((i * j) % 3 + (i + j) % 2) % 2 == 0;
  }
}

static uint8_t data[2048]; static int ndata;

static void make_impl(int v, bool test, int mp) {
  N = v * 4 + 17;
  memset(M, -1, sizeof M);
  probe(0, 0); probe(N - 7, 0); probe(0, N - 7);
  int np = npos(v);
  for (int a = 0; a < np; a++) for (int b = 0; b < np; b++) {
    int row = POS[v - 1][a], col = POS[v - 1][b];
    if (AT(row, col) != -1) continue;
    for (int r = -2; r <= 2; r++) for (int c = -2; c <= 2; c++)
      AT(row + r, col + c) = r == -2 || r == 2 || c == -2 || c == 2 || (r == 0 && c == 0);
  }
  for (int r = 8; r < N - 8; r++) if (AT(r, 6) == -1) AT(r, 6) = r % 2 == 0;
  for (int c = 8; c < N - 8; c++) if (AT(6, c) == -1) AT(6, c) = c % 2 == 0;
  /* type info: level M is 0 */
  unsigned bits = bch_type_info((0u << 3) | (unsigned)mp);
  for (int i = 0; i < 15; i++) {
    int m = !test && ((bits >> i) & 1);
    if (i < 6) AT(i, 8) = m; else if (i < 8) AT(i + 1, 8) = m; else AT(N - 15 + i, 8) = m;
  }
  for (int i = 0; i < 15; i++) {
    int m = !test && ((bits >> i) & 1);
    if (i < 8) AT(8, N - i - 1) = m; else if (i < 9) AT(8, 15 - i - 1 + 1) = m; else AT(8, 15 - i - 1) = m;
  }
  AT(N - 8, 8) = !test;
  if (v >= 7) {
    unsigned tb = bch_type_number((unsigned)v);
    for (int i = 0; i < 18; i++) { int m = !test && ((tb >> i) & 1); AT(i / 3, i % 3 + N - 8 - 3) = m; }
    for (int i = 0; i < 18; i++) { int m = !test && ((tb >> i) & 1); AT(i % 3 + N - 8 - 3, i / 3) = m; }
  }
  /* mapData */
  int inc = -1, row = N - 1, bit = 7, byte = 0;
  for (int col = N - 1; col > 0; col -= 2) {
    if (col == 6) col -= 1;
    for (;;) {
      for (int c = 0; c < 2; c++) {
        if (AT(row, col - c) == -1) {
          bool dark = false;
          if (byte < ndata) dark = ((data[byte] >> bit) & 1) == 1;
          if (mask(mp, row, col - c)) dark = !dark;
          AT(row, col - c) = dark;
          if (--bit == -1) { byte++; bit = 7; }
        }
      }
      row += inc;
      if (row < 0 || N <= row) { row -= inc; inc = -inc; break; }
    }
  }
}

static int lost_point(void) {
  int lost = 0;
  for (int row = 0; row < N; row++) for (int col = 0; col < N; col++) {
    int same = 0; int8_t dark = AT(row, col);
    for (int r = -1; r <= 1; r++) {
      if (row + r < 0 || N <= row + r) continue;
      for (int c = -1; c <= 1; c++) {
        if (col + c < 0 || N <= col + c) continue;
        if (r == 0 && c == 0) continue;
        if (dark == AT(row + r, col + c)) same++;
      }
    }
    if (same > 5) lost += 3 + same - 5;
  }
  for (int row = 0; row < N - 1; row++) for (int col = 0; col < N - 1; col++) {
    int cnt = AT(row, col) + AT(row + 1, col) + AT(row, col + 1) + AT(row + 1, col + 1);
    if (cnt == 0 || cnt == 4) lost += 3;
  }
  for (int row = 0; row < N; row++) for (int col = 0; col < N - 6; col++)
    if (AT(row, col) && !AT(row, col + 1) && AT(row, col + 2) && AT(row, col + 3) && AT(row, col + 4) && !AT(row, col + 5) && AT(row, col + 6)) lost += 40;
  for (int col = 0; col < N; col++) for (int row = 0; row < N - 6; row++)
    if (AT(row, col) && !AT(row + 1, col) && AT(row + 2, col) && AT(row + 3, col) && AT(row + 4, col) && !AT(row + 5, col) && AT(row + 6, col)) lost += 40;
  int dc = 0;
  for (int i = 0; i < N * N; i++) dc += M[i] == 1;
  double ratio = (double)(dc * 100) / N / N - 50; if (ratio < 0) ratio = -ratio;
  lost += (int)(ratio / 5 * 10);
  return lost;
}

/* the bit buffer, the codewords and the RS interleave (createData / createBytes) */
static uint8_t bb[2048]; static int bbits;
static void put(unsigned num, int len) { for (int i = 0; i < len; i++) { int b = (num >> (len - i - 1)) & 1; if (b) bb[bbits / 8] |= (uint8_t)(0x80 >> (bbits % 8)); bbits++; } }

static int poly_mod(const int *num, int nlen, const int *e, int elen, int *out) {
  /* Polynomial(num, elen - 1).mod(e): returns the remainder's length in out */
  int buf[512]; int off = 0;
  while (off < nlen && num[off] == 0) off++;
  int len = nlen - off + (elen - 1);
  if (len <= 0) { out[0] = 0; return 1; }
  for (int i = 0; i < nlen - off; i++) buf[i] = num[off + i];
  for (int i = nlen - off; i < len; i++) buf[i] = 0;
  for (;;) {
    if (len - elen < 0) break;
    int ratio = glog(buf[0]) - glog(e[0]);
    for (int i = 0; i < elen; i++) buf[i] ^= gexp(glog(e[i]) + ratio);
    int o = 0; while (o < len && buf[o] == 0) o++;
    if (o == len) { len = 1; buf[0] = 0; break; }
    memmove(buf, buf + o, sizeof(int) * (len - o)); len -= o;
  }
  memcpy(out, buf, sizeof(int) * len);
  return len;
}

int qr_make(const char *text, uint8_t *out, int cap) {
  init_tables();
  int n = (int)strlen(text);
  int v;
  for (v = 1; v <= 15; v++) {
    block b[16]; int nb = blocks(v, b), total = 0;
    for (int i = 0; i < nb; i++) total += b[i].data;
    int lenbits = v < 10 ? 8 : 16;
    if (4 + lenbits + n * 8 <= total * 8) break;
  }
  if (v > 15) return 0;
  block b[16]; int nb = blocks(v, b), total = 0;
  for (int i = 0; i < nb; i++) total += b[i].data;
  memset(bb, 0, sizeof bb); bbits = 0;
  put(4, 4); put((unsigned)n, v < 10 ? 8 : 16);
  for (int i = 0; i < n; i++) put((uint8_t)text[i], 8);
  if (bbits + 4 <= total * 8) put(0, 4);
  while (bbits % 8) bbits++;
  for (;;) {
    if (bbits >= total * 8) break;
    put(0xEC, 8);
    if (bbits >= total * 8) break;
    put(0x11, 8);
  }
  /* createBytes */
  static int dc[16][128], ec[16][128]; int dcn[16], ecn[16], maxdc = 0, maxec = 0, off = 0, totalcode = 0;
  for (int r = 0; r < nb; r++) {
    int d = b[r].data, e = b[r].total - d;
    if (d > maxdc) maxdc = d;
    if (e > maxec) maxec = e;
    dcn[r] = d; ecn[r] = e; totalcode += b[r].total;
    for (int i = 0; i < d; i++) dc[r][i] = bb[off + i];
    off += d;
    /* the generator polynomial of e error codewords */
    int g[128] = {1}; int gl = 1;
    for (int i = 0; i < e; i++) {
      int nx[128] = {0};
      int m2[2] = {1, gexp(i)};
      for (int a = 0; a < gl; a++) for (int c = 0; c < 2; c++) nx[a + c] ^= gexp(glog(g[a]) + glog(m2[c]));
      gl++; memcpy(g, nx, sizeof(int) * gl);
    }
    int mod[512]; int ml = poly_mod(dc[r], d, g, gl, mod);
    for (int i = 0; i < e; i++) { int mi = i + ml - e; ec[r][i] = mi >= 0 ? mod[mi] : 0; }
  }
  ndata = 0;
  for (int i = 0; i < maxdc; i++) for (int r = 0; r < nb; r++) if (i < dcn[r]) data[ndata++] = (uint8_t)dc[r][i];
  for (int i = 0; i < maxec; i++) for (int r = 0; r < nb; r++) if (i < ecn[r]) data[ndata++] = (uint8_t)ec[r][i];
  (void)totalcode;
  /* the mask with the lowest lost point */
  int best = 0, min = 0;
  for (int i = 0; i < 8; i++) {
    make_impl(v, true, i);
    int lp = lost_point();
    if (i == 0 || min > lp) { min = lp; best = i; }
  }
  make_impl(v, false, best);
  if (N * N > cap) return 0;
  for (int i = 0; i < N * N; i++) out[i] = M[i] == 1;
  return N;
}

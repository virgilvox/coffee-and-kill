/* input.c: see input.h. The page's qe object: the key maps Rl (P1 of two),
 * Xl (P2 of two) and Sl (one player: every key), the standard gamepad
 * mapping, and the edge tracking of presses (O8) and pad buttons (Jl). */
#include "input.h"
#include "ck.h"
#include <string.h>
#include <math.h>

int input_mode = 1;
bool (*input_remote)(int slot, intent_t *out);

static uint8_t kcur[32], kprev[32];
static wc_pad_t pcur[4], pprev[4];
static int kev[64], nkev;
static char text[64]; static int ntext;
static ck_pointer ptr;
static int nav_ox[4], nav_oy[4];     /* the menu stick's last direction, for edges */
static double nav_rep[4];            /* when a held direction repeats next */
static bool jl[2][5];                /* Jl: each pad's last state of the five attack buttons */
int input_last_dev = DEV_KEYS;
static bool dev_seen;
static menu_ev mev[48]; static int nmev;
static double back_t0 = -1;

#define DOWN(k, c) (((k)[(c) >> 3] >> ((c) & 7)) & 1)

void input_init(void) {}

static char key_char(int c, bool shift) {
  if (c >= WC_KEY_A && c <= WC_KEY_Z) return (char)('A' + c - WC_KEY_A);   /* the fields upper-case anyway */
  if (c >= WC_KEY_1 && c <= WC_KEY_9) return (char)('1' + c - WC_KEY_1);
  if (c == WC_KEY_0) return '0';
  if (c == WC_KEY_MINUS) return shift ? '_' : '-';
  if (c == WC_KEY_SPACE) return ' ';
  return 0;
}

void input_frame(const wc_pad_t *pads, const uint8_t *keys, const wc_pointer_t *p) {
  memcpy(kprev, kcur, 32); memcpy(kcur, keys, 32);
  memcpy(pprev, pcur, sizeof pcur); memcpy(pcur, pads, sizeof pcur);
  nkev = 0; ntext = 0;
  bool shift = DOWN(kcur, CK_KEY_LSHIFT) || DOWN(kcur, CK_KEY_RSHIFT);
  for (int c = 1; c < 256; c++) if (DOWN(kcur, c) && !DOWN(kprev, c)) {
    if (nkev < 64) kev[nkev++] = c;
    char ch = key_char(c, shift);
    if (ch && ntext < 63) text[ntext++] = ch;
  }
  text[ntext] = 0;
  if (nkev) input_last_dev = DEV_KEYS;
  /* a pad that reports itself at boot is the device until something else is used (the station) */
  if (!dev_seen) { dev_seen = true; for (int i = 0; i < 4; i++) if (pcur[i].connected) input_last_dev = DEV_PAD; }
  /* the pads' menu events. Every pad is read, connected or not: the station's core reports only
   * port 0 as connected, and a pad that is not there reads all zeros. */
  nmev = 0;
  double now = ck_now();
  for (int i = 0; i < 4; i++) {
    const wc_pad_t *q = &pcur[i];
    int pl = input_mode == 1 ? 0 : i & 1;
    int dx = 0, dy = 0;
    if (q->buttons & WC_BTN_LEFT) dx = -1; else if (q->buttons & WC_BTN_RIGHT) dx = 1;
    else if (q->left_x < -16000) dx = -1; else if (q->left_x > 16000) dx = 1;
    if (q->buttons & WC_BTN_UP) dy = -1; else if (q->buttons & WC_BTN_DOWN) dy = 1;
    else if (q->left_y < -16000) dy = -1; else if (q->left_y > 16000) dy = 1;
    bool fire = false;
    if (dx != nav_ox[i] || dy != nav_oy[i]) { fire = dx || dy; nav_rep[i] = now + 0.38; }
    else if ((dx || dy) && now >= nav_rep[i]) { fire = true; nav_rep[i] = now + 0.11; }
    nav_ox[i] = dx; nav_oy[i] = dy;
    if (fire) {
      if (dy) input_push_menu(pl, dy < 0 ? ME_UP : ME_DOWN, DEV_PAD);
      else input_push_menu(pl, dx < 0 ? ME_LEFT : ME_RIGHT, DEV_PAD);
    }
    static const struct { uint32_t b; int ev; } MAP[] = {{WC_BTN_A, ME_OK}, {WC_BTN_B, ME_BACK}, {WC_BTN_Y, ME_SWAP},
      {WC_BTN_L, ME_PREV}, {WC_BTN_R, ME_NEXT}, {WC_BTN_START, ME_START}};
    for (int k = 0; k < (int)(sizeof MAP / sizeof MAP[0]); k++)
      if ((q->buttons & MAP[k].b) && !(pprev[i].buttons & MAP[k].b)) input_push_menu(pl, MAP[k].ev, DEV_PAD);
    if (q->buttons & ~pprev[i].buttons) input_last_dev = DEV_PAD;
  }
  /* the back button held (B on a pad, Escape): the select's hold to leave */
  bool bk = DOWN(kcur, WC_KEY_ESCAPE);
  for (int i = 0; i < 4; i++) bk |= (pcur[i].buttons & WC_BTN_B) != 0;
  if (!bk) back_t0 = -1; else if (back_t0 < 0) back_t0 = now;
  if (p && p[0].active) {
    bool d = p[0].buttons & 1, r = (p[0].buttons & 2) != 0;
    ptr.moved = ptr.active && (ptr.x != p[0].x || ptr.y != p[0].y);
    ptr.pressed = d && !ptr.down; ptr.released = !d && ptr.down;
    ptr.rpressed = r && !ptr.rdown; ptr.rdown = r;
    ptr.down = d; ptr.active = true; ptr.x = p[0].x; ptr.y = p[0].y;
    if (ptr.moved || ptr.pressed || ptr.rpressed) input_last_dev = DEV_MOUSE;
  } else { ptr.pressed = ptr.released = ptr.rpressed = ptr.moved = false; ptr.down = ptr.rdown = false; ptr.active = false; }
}

void input_note_dev(int dev) { input_last_dev = dev; }
void input_push_menu(int player, int ev, int dev) {
  if (nmev < (int)(sizeof mev / sizeof mev[0])) mev[nmev++] = (menu_ev){player, ev, dev};
  input_last_dev = dev;
}
int input_menu_events(const menu_ev **e) { *e = mev; return nmev; }
float input_back_held(void) { return back_t0 < 0 ? 0 : (float)(ck_now() - back_t0); }

void input_end_frame(void) { nkev = 0; nmev = 0; }
int input_key_events(const int **codes) { *codes = kev; return nkev; }
void input_push_key(int code) { if (nkev < 64) kev[nkev++] = code; }
int input_text(const char **s) { *s = text; return ntext; }
bool input_key_down(int c) { return c > 0 && c < 256 && DOWN(kcur, c); }
bool input_key_pressed(int c) { return c > 0 && c < 256 && DOWN(kcur, c) && !DOWN(kprev, c); }
bool input_any_key(void) { return nkev > 0; }
bool input_pad_pressed(int pad, uint32_t b) { return pad >= 0 && pad < 4 && (pcur[pad].buttons & b) && !(pprev[pad].buttons & b); }
bool input_pad_down(int pad, uint32_t b) { return pad >= 0 && pad < 4 && (pcur[pad].buttons & b); }
bool input_pad_connected(int pad) { return pad >= 0 && pad < 4 && pcur[pad].connected; }
const ck_pointer *input_pointer(void) { return &ptr; }

/* the key maps, KeyboardEvent.code -> HID */
typedef struct { int left[2], right[2], up[2], down[2], light[3], heavy[3], kick[3], block[3], dodge[3], special[3]; } keymap;
static const keymap RL = {   /* Rl: P1 of two players */
  {WC_KEY_A}, {WC_KEY_D}, {WC_KEY_W}, {WC_KEY_S}, {WC_KEY_Q}, {WC_KEY_E}, {WC_KEY_R}, {WC_KEY_SPACE}, {CK_KEY_LSHIFT}, {WC_KEY_F}};
static const keymap XL = {   /* Xl: P2 of two players */
  {WC_KEY_LEFT}, {WC_KEY_RIGHT}, {WC_KEY_UP}, {WC_KEY_DOWN}, {WC_KEY_COMMA}, {WC_KEY_PERIOD}, {WC_KEY_SLASH},
  {CK_KEY_RSHIFT}, {WC_KEY_ENTER}, {WC_KEY_QUOTE}};
static const keymap SL = {   /* Sl: one player, every key */
  {WC_KEY_A, WC_KEY_LEFT}, {WC_KEY_D, WC_KEY_RIGHT}, {WC_KEY_W, WC_KEY_UP}, {WC_KEY_S, WC_KEY_DOWN},
  {WC_KEY_Q, WC_KEY_J, WC_KEY_Z}, {WC_KEY_E, WC_KEY_K, WC_KEY_X}, {WC_KEY_R, WC_KEY_L, WC_KEY_C},
  {WC_KEY_SPACE, WC_KEY_U, WC_KEY_V}, {CK_KEY_LSHIFT, CK_KEY_RSHIFT, WC_KEY_I}, {WC_KEY_F, WC_KEY_O, WC_KEY_B}};

static bool any_down(const int *k, int n) { for (int i = 0; i < n; i++) if (k[i] && DOWN(kcur, k[i])) return true; return false; }
static bool any_pressed(const int *k, int n) { for (int i = 0; i < n; i++) if (k[i] && DOWN(kcur, k[i]) && !DOWN(kprev, k[i])) return true; return false; }

static int pdev[2];
int input_dev_of(int slot) { return pdev[slot & 1]; }
void input_dev_reset(void) { pdev[0] = pdev[1] = input_last_dev == DEV_MOUSE ? DEV_KEYS : input_last_dev; }
static bool any_key(const keymap *A) {
  const int *k = &A->left[0];
  for (int i = 0; i < (int)(sizeof *A / sizeof(int)); i++) if (k[i] && DOWN(kcur, k[i])) return true;
  return false;
}

intent_t input_poll(int s) {
  const keymap *A = input_mode == 1 ? &SL : s == 0 ? &RL : &XL;
  intent_t P = {0};
  if (any_key(A)) pdev[s & 1] = DEV_KEYS;
  P.mx = (any_down(A->right, 2) ? 1.f : 0.f) - (any_down(A->left, 2) ? 1.f : 0.f);
  P.my = (any_down(A->up, 2) ? 1.f : 0.f) - (any_down(A->down, 2) ? 1.f : 0.f);
  P.block = any_down(A->block, 3);
  P.light = any_pressed(A->light, 3); P.heavy = any_pressed(A->heavy, 3); P.kick = any_pressed(A->kick, 3);
  P.dodge = any_pressed(A->dodge, 3); P.special = any_pressed(A->special, 3);
  /* the pads: one player reads pads 0 and 1, two players pad s */
  int list[2] = {0, 1}, nl = input_mode == 1 ? 2 : 1;
  if (input_mode != 1) list[0] = s;
  for (int k = 0; k < nl; k++) {
    int i = list[k];
    if (i < 0 || i > 3) continue;   /* connected or not: see the menu events above */
    const wc_pad_t *n = &pcur[i];
    float r = n->left_x / 32767.f, v = n->left_y / 32767.f, o = 0.25f;
    if (n->buttons || fabsf(r) > o || fabsf(v) > o || n->right_trigger > WC_TRIGGER_PRESSED || n->left_trigger > WC_TRIGGER_PRESSED) pdev[s & 1] = DEV_PAD;
    if (fabsf(r) > o) P.mx = fabsf(r) > fabsf(P.mx) ? ck_sign(r) * fminf(1, (fabsf(r) - o) / (1 - o) + 0.2f) : P.mx;
    if (fabsf(v) > o) P.my = fabsf(v) > fabsf(P.my) ? -ck_sign(v) * fminf(1, (fabsf(v) - o) / (1 - o) + 0.2f) : P.my;
    /* the standard mapping: 0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 6 LT, 7 RT, 12..15 the d-pad */
    uint32_t b = n->buttons;
    if (b & WC_BTN_LEFT) P.mx = -1;
    if (b & WC_BTN_RIGHT) P.mx = 1;
    if (b & WC_BTN_UP) P.my = 1;
    if (b & WC_BTN_DOWN) P.my = -1;
    bool lt = n->left_trigger > WC_TRIGGER_PRESSED, rt = n->right_trigger > WC_TRIGGER_PRESSED;
    P.block = P.block || (b & WC_BTN_L) || (b & WC_BTN_R) || lt;
    bool now[5] = {(b & WC_BTN_X) != 0, (b & WC_BTN_Y) != 0, (b & WC_BTN_B) != 0, (b & WC_BTN_A) != 0, rt};   /* light heavy kick dodge special */
    bool *z = jl[i & 1];
    bool *dst[5] = {&P.light, &P.heavy, &P.kick, &P.dodge, &P.special};
    for (int c = 0; c < 5; c++) { if (now[c] && !z[c]) *dst[c] = true; z[c] = now[c]; }
  }
  /* the touch controls of one player and the phone pads (qe.remote) */
  intent_t rem;
  if (input_remote && input_remote(s, &rem)) {
    if (rem.mx || rem.my || rem.block || rem.light || rem.heavy || rem.kick || rem.dodge || rem.special) pdev[s & 1] = DEV_PHONE;
    if (fabsf(rem.mx) > fabsf(P.mx)) P.mx = rem.mx;
    if (fabsf(rem.my) > fabsf(P.my)) P.my = rem.my;
    P.block = P.block || rem.block;
    P.light |= rem.light; P.heavy |= rem.heavy; P.kick |= rem.kick; P.dodge |= rem.dodge; P.special |= rem.special;
  }
  return P;
}

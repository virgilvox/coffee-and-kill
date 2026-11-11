/* online.c: see online.h. The page's Cv (the lobby), qC (a match), zv (the
 * phone pads) and fv (the screens), over net.c. Addresses, payloads and
 * timings are the page's, so cart and web players share rooms.
 *
 * The page builds its share links from location.href; the cart uses the
 * page's public address, weird.link/coffeekill, which keeps the query.
 *
 * Carts say more than the page does: every presence, challenge and answer carries an "x" map
 * (v: the protocol, 2; c: the fighter; w: the weapon; g: the stage) that the page ignores. Two
 * carts that both send it fight with their own picks on the challenger's stage; against the
 * page (no "x") it is MOHEEB against LUIS in the workshop with the page's four weapons, so the
 * page's wire, its "w" and its snapshots, never see anything it does not know. */
#include "online.h"
#include "net.h"
#include "ui.h"
#include "game.h"
#include "input.h"
#include "fighter.h"
#include "roster.h"
#include "weapons.h"
#include "stage.h"
#include "qr.h"
#include "ck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CS "/coffeekill/v1"
#define LOBBY "LOBBY"
#define PAGE_URL "https://weird.link/coffeekill"
#define WIRE_V 2   /* this cart's extension of the page's messages (the "x" map) */

static double now_ms(void) { return ck_now() * 1000; }
static void q8(char *out) {   /* Math.random().toString(36).slice(2, 8) */
  static const char *d = "0123456789abcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 6; i++) out[i] = d[(int)(ck_rand() * 36)];
  out[6] = 0;
}
static void av(char *out) {   /* a room or pad code */
  static const char *s = "ABCDEFGHJKLMNPQRSTUVWXYZ";
  for (int i = 0; i < 4; i++) out[i] = s[(int)(ck_rand() * 24)];
  out[4] = 0;
}
static void upper_clean(char *dst, const char *src, int max, bool room) {
  int n = 0;
  for (; *src && n < max; src++) {
    char c = (char)toupper((unsigned char)*src);
    if (room && !(isalnum((unsigned char)c) || c == '_' || c == '-')) continue;
    dst[n++] = c;
  }
  dst[n] = 0;
}

/* ---- Cv: the lobby ---- */
typedef struct { char id[16], n[16], w[12], c[12]; int v; double q; int s; double seen, first; } peer;
/* what a side brings: the page's "w", and a cart's "x" */
typedef struct { int v; char c[12], w[12], g[12]; } ext_t;
static struct {
  char id[16], name[16], weapon[12], room[20];
  char fighter[12], stage[12];      /* this cart's picks (the "x" map) */
  bool in_lobby, busy;
  peer peers[16]; int npeers;
  int subs[2];
  double seq;
  double hb;                        /* the heartbeat timer (1.5 s) */
  bool pending; char pend_to[16], pend_mid[16]; double pend_at;
  /* a challenge shown to us (fv.chal) */
  bool chal; char ch_from[16], ch_n[16], ch_w[12], ch_mid[16]; double ch_at; ext_t ch_x;
  /* the match */
  bool in_match; int role; char mid[16]; char opp_id[16], opp_n[16], opp_w[12];
  int msubs[3]; double last_in; bool closed;
  int status_shown;                  /* fv: the relay error was shown */
} L;

static void fire_peers(void);
static int peer_cmp(const void *a, const void *b) { double d = ((const peer *)a)->first - ((const peer *)b)->first; return d < 0 ? -1 : d > 0; }
static void list_sorted(void) { qsort(L.peers, L.npeers, sizeof(peer), peer_cmp); }

/* the weapon the page may see: ours if it is on the page's rack, else the pipe wrench */
static const char *web_weapon(void) { int w = weapon_index(L.weapon); return w >= 0 && WEAPONS[w].web ? L.weapon : "pipe"; }
static mp_val *ext_map(void) {
  mp_val *x = mp_map();
  mp_set(x, "v", mp_num(WIRE_V)); mp_set(x, "c", mp_str(L.fighter)); mp_set(x, "w", mp_str(L.weapon)); mp_set(x, "g", mp_str(L.stage));
  return x;
}
static ext_t ext_read(const mp_val *A) {
  ext_t e = {0};
  const mp_val *x = mp_get(A, "x");
  if (x && x->t == MP_MAP) {
    e.v = (int)mp_getn(x, "v", 0);
    snprintf(e.c, sizeof e.c, "%s", mp_gets(x, "c", "")); snprintf(e.w, sizeof e.w, "%s", mp_gets(x, "w", "")); snprintf(e.g, sizeof e.g, "%s", mp_gets(x, "g", ""));
    if (roster_index(e.c) < 0 || weapon_index(e.w) < 0) e.v = 0;   /* a newer cart's pick this one lacks: fight the page's way */
  }
  return e;
}
/* the page's message plus our "x" */
static mp_val *msg(const char *k, const char *mid) {
  mp_val *m = mp_map(); mp_set(m, "k", mp_str(k)); mp_set(m, "from", mp_str(L.id)); mp_set(m, "n", mp_str(L.name));
  mp_set(m, "w", mp_str(web_weapon())); mp_set(m, "mid", mp_str(mid)); mp_set(m, "x", ext_map());
  return m;
}

static void beat(void) {
  if (!L.in_lobby) return;
  char a[96]; snprintf(a, sizeof a, CS "/r/%s/p/%s", L.room, L.id);
  mp_val *v = mp_map();
  mp_set(v, "n", mp_str(L.name)); mp_set(v, "w", mp_str(web_weapon())); mp_set(v, "q", mp_num(++L.seq));
  mp_set(v, "s", mp_num(L.busy ? 1 : 0)); mp_set(v, "f", mp_str(L.id)); mp_set(v, "x", ext_map());
  clasp_set(a, v, 8e6);
  mp_free(v);
}

static void on_presence(const mp_val *A, const char *addr, void *ctx) {
  (void)ctx;
  if (!A || A->t != MP_MAP) return;
  const char *slash = strrchr(addr, '/');
  const char *e = mp_gets(A, "f", slash ? slash + 1 : "");
  if (!strcmp(e, L.id)) return;
  if (mp_truthy(mp_get(A, "gone"))) {
    for (int i = 0; i < L.npeers; i++) if (!strcmp(L.peers[i].id, e)) { L.peers[i] = L.peers[--L.npeers]; break; }
    fire_peers();
    return;
  }
  peer *t = NULL;
  for (int i = 0; i < L.npeers; i++) if (!strcmp(L.peers[i].id, e)) t = &L.peers[i];
  double q = mp_getn(A, "q", 0), D = now_ms();
  if (!t || t->q != q) {
    double first = t ? t->first : D;
    if (!t) { if (L.npeers >= 16) return; t = &L.peers[L.npeers++]; }
    snprintf(t->id, 16, "%s", e); snprintf(t->n, 16, "%s", mp_gets(A, "n", "?")); snprintf(t->w, 12, "%s", mp_gets(A, "w", ""));
    ext_t x = ext_read(A); t->v = x.v; snprintf(t->c, sizeof t->c, "%s", x.v ? x.c : "");
    if (x.v) snprintf(t->w, 12, "%s", x.w);
    t->q = q; t->s = (int)mp_getn(A, "s", 0); t->seen = D; t->first = first;
    fire_peers();
  }
}

static void start_match(int role, const char *mid, const char *opp_id, const char *opp_n, const char *opp_w, ext_t opp_x);

static void inbox_send(const char *to, mp_val *v) {
  char a[96]; snprintf(a, sizeof a, CS "/r/%s/p/%s/in", L.room, to);
  clasp_emit(a, v); mp_free(v);
}
static void on_inbox(const mp_val *A, const char *addr, void *ctx) {
  (void)addr; (void)ctx;
  if (!A || A->t != MP_MAP) return;
  const char *k = mp_gets(A, "k", "");
  if (!strcmp(k, "ch")) {
    if (L.busy) {
      mp_val *m = mp_map(); mp_set(m, "k", mp_str("no")); mp_set(m, "from", mp_str(L.id));
      mp_set(m, "mid", mp_str(mp_gets(A, "mid", ""))); mp_set(m, "busy", mp_num(1));
      inbox_send(mp_gets(A, "from", ""), m);
      return;
    }
    /* fv.incoming */
    L.chal = true; L.ch_at = now_ms();
    snprintf(L.ch_from, 16, "%s", mp_gets(A, "from", "")); snprintf(L.ch_n, 16, "%s", mp_gets(A, "n", "PLAYER"));
    snprintf(L.ch_w, 12, "%s", mp_gets(A, "w", "pipe")); snprintf(L.ch_mid, 16, "%s", mp_gets(A, "mid", ""));
    L.ch_x = ext_read(A);
    ui_online.challenge = true; snprintf(ui_online.chtxt, sizeof ui_online.chtxt, "%s CHALLENGES YOU", L.ch_n);
  } else if (!strcmp(k, "ok") && L.pending && !strcmp(L.pend_mid, mp_gets(A, "mid", ""))) {
    L.pending = false;
    start_match(NET_HOST, mp_gets(A, "mid", ""), mp_gets(A, "from", ""), mp_gets(A, "n", "PLAYER"), mp_gets(A, "w", "pipe"), ext_read(A));
  } else if (!strcmp(k, "no") && L.pending && !strcmp(L.pend_mid, mp_gets(A, "mid", ""))) {
    L.pending = false;
    snprintf(ui_online.status, sizeof ui_online.status, "%s", mp_truthy(mp_get(A, "busy")) ? "BUSY" : "DECLINED");
  }
}

static void leave_room(void) {
  if (L.in_lobby) {
    char a[96]; snprintf(a, sizeof a, CS "/r/%s/p/%s", L.room, L.id);
    mp_val *v = mp_map(); mp_set(v, "gone", mp_bool(true)); mp_set(v, "f", mp_str(L.id)); mp_set(v, "q", mp_num(++L.seq));
    clasp_set(a, v, 3e6); mp_free(v);
  }
  for (int i = 0; i < 2; i++) if (L.subs[i]) { clasp_unsub(L.subs[i]); L.subs[i] = 0; }
  L.in_lobby = false; L.npeers = 0; L.pending = false;
}

static void lobby_join(const char *room, const char *name, const char *weapon) {
  leave_room();
  upper_clean(L.room, room, 16, true);
  if (!L.room[0]) snprintf(L.room, sizeof L.room, "OPEN");
  upper_clean(L.name, name && *name ? name : "PLAYER", 14, false);
  if (weapon && *weapon && weapon != L.weapon) snprintf(L.weapon, sizeof L.weapon, "%s", weapon);
  L.npeers = 0; L.in_lobby = true;
  char t[64], p[96];
  snprintf(t, sizeof t, CS "/r/%s", L.room);
  snprintf(p, sizeof p, "%s/p/*", t); L.subs[0] = clasp_sub(p, on_presence, NULL);
  snprintf(p, sizeof p, "%s/p/%s/in", t, L.id); L.subs[1] = clasp_sub(p, on_inbox, NULL);
  beat(); L.hb = 0;
  fire_peers();
}

/* ---- qC: a match ---- */
static void on_m_snap(const mp_val *g, const char *a, void *c) {
  (void)a; (void)c;
  if (g && strcmp(mp_gets(g, "src", ""), L.id) && L.role == NET_GUEST) {
    L.last_in = now_ms(); game_on_snap(g);
#ifdef CK_ONLINE_TEST
    static int n; if (n++ % 50 == 0) ck_log("online: snap %d ph %s rd %d tl %.1f", (int)mp_getn(g, "n", 0), mp_gets(g, "ph", "?"), (int)mp_getn(g, "rd", 0), mp_getn(g, "tl", 0));
#endif
  }
}
static void on_m_intent(const mp_val *g, const char *a, void *c) {
  (void)a; (void)c;
  if (g && strcmp(mp_gets(g, "src", ""), L.id) && L.role == NET_HOST) { L.last_in = now_ms(); game_on_intent(g); }
}
static void on_m_ctl(const mp_val *g, const char *a, void *c) {
  (void)a; (void)c;
  if (g && strcmp(mp_gets(g, "src", ""), L.id)) { L.last_in = now_ms(); game_on_ctl(g); }
}

static void start_match(int role, const char *mid, const char *opp_id, const char *opp_n, const char *opp_w, ext_t opp_x) {
  L.busy = true; beat();
  L.in_match = true; L.role = role; L.closed = false; L.last_in = now_ms();
  snprintf(L.mid, 16, "%s", mid); snprintf(L.opp_id, 16, "%s", opp_id); snprintf(L.opp_n, 16, "%s", opp_n); snprintf(L.opp_w, 12, "%s", opp_w);
  char p[96];
  snprintf(p, sizeof p, CS "/m/%s/s", mid); L.msubs[0] = clasp_sub(p, on_m_snap, NULL);
  snprintf(p, sizeof p, CS "/m/%s/i", mid); L.msubs[1] = clasp_sub(p, on_m_intent, NULL);
  snprintf(p, sizeof p, CS "/m/%s/c", mid); L.msubs[2] = clasp_sub(p, on_m_ctl, NULL);
  /* fv.onMatch */
  ui_online.challenge = false;
  /* two carts: each its own picks on the challenger's (the host's) stage; else the page's match */
  online_pick me, them;
  if (opp_x.v >= WIRE_V) {
    snprintf(me.fighter, 12, "%s", L.fighter); snprintf(me.weapon, 12, "%s", L.weapon);
    snprintf(them.fighter, 12, "%s", opp_x.c); snprintf(them.weapon, 12, "%s", opp_x.w);
    const char *g = role == NET_HOST ? L.stage : opp_x.g;
    snprintf(me.stage, 12, "%s", stage_index(g) >= 0 ? g : "workshop"); snprintf(them.stage, 12, "%s", me.stage);
  } else {
    snprintf(me.fighter, 12, "%s", role == NET_HOST ? "moheeb" : "luis"); snprintf(me.weapon, 12, "%s", web_weapon());
    snprintf(them.fighter, 12, "%s", role == NET_HOST ? "luis" : "moheeb");
    snprintf(them.weapon, 12, "%s", weapon_index(opp_w) >= 0 && WEAPONS[weapon_index(opp_w)].web ? opp_w : "pipe");
    snprintf(me.stage, 12, "workshop"); snprintf(them.stage, 12, "workshop");
  }
  game_begin_online(role, L.name, &me, opp_n, &them);
}

bool online_in_match(void) { return L.in_match; }
int online_role(void) { return L.in_match ? L.role : NET_NONE; }
const char *online_my_name(void) { return L.name; }
const char *online_opp_name(void) { return L.opp_n; }
static void msend(const char *ch, mp_val *v, bool stream) {
  if (!L.in_match) { mp_free(v); return; }
  mp_set(v, "src", mp_str(L.id));
  char a[96]; snprintf(a, sizeof a, CS "/m/%s/%s", L.mid, ch);
  if (stream) clasp_stream(a, v); else clasp_emit(a, v);
  mp_free(v);
}
void online_snap(mp_val *v) { msend("s", v, true); }
void online_intent(mp_val *v) { msend("i", v, true); }
void online_ctl(mp_val *v) { msend("c", v, false); }
double online_idle_ms(void) { return now_ms() - L.last_in; }
void online_end_match(void) {
  if (L.in_match && !L.closed) {
    L.closed = true;
    mp_val *b = mp_map(); mp_set(b, "k", mp_str("bye")); online_ctl(b);
    for (int i = 0; i < 3; i++) if (L.msubs[i]) { clasp_unsub(L.msubs[i]); L.msubs[i] = 0; }
  }
  L.in_match = false;
  L.busy = false; beat();
}

/* ---- zv: the phone pads ---- */
typedef struct { bool have; float mx, my; bool b; int c[5], seen[5]; double t; bool fresh; } pad;
static struct { bool on; char code[8]; int sub; pad p[2]; double st_timer; } Z;
static const char *PK[5] = {"l", "h", "k", "d", "s"};
static void on_pad(const mp_val *P, const char *e, void *ctx) {
  (void)e; (void)ctx;
  if (!P || P->t != MP_MAP) return;
  int t = mp_getn(P, "p", 1) == 2 ? 1 : 0;
  pad *D = &Z.p[t];
  if (!D->have) { memset(D, 0, sizeof *D); D->have = true; D->fresh = true; }
  const mp_val *c = mp_get(P, "c");
  if (D->fresh) { for (int i = 0; i < 5; i++) D->seen[i] = (int)mp_getn(c, PK[i], D->seen[i]); D->fresh = false; }
  D->mx = (float)mp_getn(P, "mx", 0); D->my = (float)mp_getn(P, "my", 0); D->b = mp_getn(P, "b", 0) != 0;
  if (c) for (int i = 0; i < 5; i++) D->c[i] = (int)mp_getn(c, PK[i], D->c[i]);
  D->t = now_ms();
}
static bool pad_connected(int A) { return Z.p[A].have && now_ms() - Z.p[A].t < 4e3; }
static bool pads_poll(int A, intent_t *out) {
  if (A < 0 || A > 1) return false;
  pad *P = &Z.p[A];
  if (!P->have || now_ms() - P->t > 4e3) return false;
  memset(out, 0, sizeof *out);
  out->mx = P->mx; out->my = P->my; out->block = P->b;
  bool *dst[5] = {&out->light, &out->heavy, &out->kick, &out->dodge, &out->special};
  for (int i = 0; i < 5; i++) if (P->c[i] > P->seen[i]) { *dst[i] = true; P->seen[i] = P->c[i]; }
  return true;
}
static void pad_status(void) {
  if (!Z.on) return;
  bool a = pad_connected(0), p = pad_connected(1);
  snprintf(ui_pads.st, sizeof ui_pads.st, "P1 %s   P2 %s%s", a ? "CONNECTED" : "WAITING", p ? "CONNECTED" : "WAITING",
           a || p ? "" : "   (ADD &p=2 FOR PLAYER 2)");
}

/* ---- fv: the screens ---- */
static void status(const char *s) { snprintf(ui_online.status, sizeof ui_online.status, "%s", s); }
static const char *room_msg(void) {
  static char b[160];
  if (!strcmp(L.room, LOBBY)) return "IN THE MAIN LOBBY. ANYONE WHO OPENS ONLINE LANDS HERE. CHALLENGE A PLAYER BELOW.";
  snprintf(b, sizeof b, "IN PRIVATE ROOM %s. SEND THE LINK OR CODE TO YOUR FRIEND.", L.room);
  return b;
}
static void show_link(void) {
  ui_online.link_row = L.in_lobby;
  if (L.in_lobby) snprintf(ui_online.link, sizeof ui_online.link, PAGE_URL "?room=%s", L.room);
}
static void fire_peers(void) {
  list_sorted();
#ifdef CK_ONLINE_TEST
  for (int i = 0; i < L.npeers; i++) ck_log("online: peer %s %s %s%s", L.peers[i].id, L.peers[i].n, L.peers[i].w, L.peers[i].s ? " busy" : "");
#endif
  ui_online.npeers = L.in_lobby ? L.npeers : 0;
  for (int i = 0; i < ui_online.npeers; i++) {
    snprintf(ui_online.peers[i].id, 16, "%s", L.peers[i].id); snprintf(ui_online.peers[i].n, 16, "%s", L.peers[i].n);
    snprintf(ui_online.peers[i].w, 12, "%s", L.peers[i].w); ui_online.peers[i].busy = L.peers[i].s != 0;
    snprintf(ui_online.peers[i].c, 12, "%s", L.peers[i].c); ui_online.peers[i].v = L.peers[i].v;
  }
}

static bool pending_join; static double join_wait;
static void do_join_now(void) {
  lobby_join(ui_online.room, ui_online.name, L.weapon);
  snprintf(ui_online.room, sizeof ui_online.room, "%s", L.room);
  status(room_msg()); fire_peers(); show_link();
}
void online_join(void) {
  char A[16]; upper_clean(A, ui_online.name[0] ? ui_online.name : "PLAYER", 14, false);
  char *e = A + strlen(A); while (e > A && e[-1] == ' ') *--e = 0;   /* .trim() */
  if (!ui_online.room[0]) av(ui_online.room);
  ck_save_set("ck_name", A);
  status("CONNECTING");
  clasp_connect();
  pending_join = true; join_wait = 0;
}
void online_open(void) {
  ui_show("s-online");
  if (!ui_online.name[0]) snprintf(ui_online.name, sizeof ui_online.name, "PLAYER%d", (int)(10 + ck_rand() * 90));
  fire_peers(); show_link();
  if (L.in_lobby) { status(room_msg()); return; }
  snprintf(ui_online.room, sizeof ui_online.room, LOBBY);
  online_join();
}
void online_back(void) { leave_room(); status(""); show_link(); }
void online_main_lobby(void) { snprintf(ui_online.room, sizeof ui_online.room, LOBBY); online_join(); }
void online_new_code(void) { av(ui_online.room); online_join(); }
/* the page copies to the clipboard and flashes COPIED; a cart has no clipboard, so
 * it takes the page's fallback ("PRESS CTRL C") as a toast; the link is on screen */
void online_copy_link(void) { ui_toast("PRESS CTRL C"); }
void online_copy_pad(void) { ui_toast("PRESS CTRL C"); }
void online_accept(void) {
  if (!L.chal) return;
  L.chal = false; ui_online.challenge = false;
  inbox_send(L.ch_from, msg("ok", L.ch_mid));
  start_match(NET_GUEST, L.ch_mid, L.ch_from, L.ch_n, L.ch_w, L.ch_x);
}
void online_decline(void) {
  if (!L.chal) return;
  L.chal = false; ui_online.challenge = false;
  inbox_send(L.ch_from, msg("no", L.ch_mid));
}
static void challenge(const char *A) {
  char P[16]; q8(P); q8(P + 6); P[12] = 0;
  L.pending = true; snprintf(L.pend_to, 16, "%s", A); snprintf(L.pend_mid, 16, "%s", P); L.pend_at = now_ms();
  inbox_send(A, msg("ch", P));
  status("CHALLENGE SENT. WAITING FOR AN ANSWER.");
}
static void ui_weapon(const char *w) {
  if (weapon_index(w) < 0) return;
  snprintf(L.weapon, sizeof L.weapon, "%s", w); snprintf(ui_online.weapon, sizeof ui_online.weapon, "%s", w);
  if (WEAPONS[weapon_index(w)].web) ck_save_set("ck_weapon", w);
  beat();
}
static void ui_fighter(const char *c) {
  int r = roster_index(c);
  if (r < 0) return;
  snprintf(L.fighter, sizeof L.fighter, "%s", c); snprintf(ui_online.fighter, sizeof ui_online.fighter, "%s", c);
  ui_weapon(ROSTER[r].weapon);
}
static void ui_stage(const char *g) {
  if (stage_index(g) < 0) return;
  snprintf(L.stage, sizeof L.stage, "%s", g); snprintf(ui_online.stage, sizeof ui_online.stage, "%s", g);
  beat();
}
void online_open_pads(void) {
  ui_show("s-pads");
  if (!Z.on) {
    Z.on = true; av(Z.code);
    snprintf(ui_pads.code, sizeof ui_pads.code, "%s", Z.code);
    snprintf(ui_pads.url, sizeof ui_pads.url, PAGE_URL "?pad=%s", Z.code);
    ui_pads.qr_n = qr_make(ui_pads.url, ui_pads.qr, (int)sizeof ui_pads.qr);
    input_remote = pads_poll;
    snprintf(ui_pads.st, sizeof ui_pads.st, "CONNECTING");
    clasp_connect();
    char p[64]; snprintf(p, sizeof p, CS "/pad/%s/in/*", Z.code);
    Z.sub = clasp_sub(p, on_pad, NULL);
  }
  pad_status();
}
void online_leave_room_status(const char *msg) {
  ui_hud(false); online_open();
  if (msg && *msg) status(msg);
}

void online_init(void) {
  q8(L.id); q8(L.id + 6); L.id[12] = 0;
  snprintf(L.name, sizeof L.name, "PLAYER");
  snprintf(L.weapon, sizeof L.weapon, "%s", ck_save_get("ck_weapon", "pipe"));
  if (weapon_index(L.weapon) < 0) snprintf(L.weapon, sizeof L.weapon, "pipe");
  snprintf(ui_online.weapon, sizeof ui_online.weapon, "%s", L.weapon);
  snprintf(L.fighter, sizeof L.fighter, "moheeb"); snprintf(ui_online.fighter, sizeof ui_online.fighter, "moheeb");
  snprintf(L.stage, sizeof L.stage, "workshop"); snprintf(ui_online.stage, sizeof ui_online.stage, "workshop");
  snprintf(ui_online.name, sizeof ui_online.name, "%s", ck_save_get("ck_name", ""));
  ui_online.room[0] = 0;
  ui_on_olweapon = ui_weapon; ui_on_olfighter = ui_fighter; ui_on_olstage = ui_stage;
  ui_on_challenge = challenge;
  ui_on_room_enter = online_join;
}

void online_update(float dt) {
  clasp_update(dt);
  int st = clasp_status();
  if (pending_join) {
    join_wait += dt;
    if (st == NET_ONLINE) { pending_join = false; do_join_now(); }
    else if (st == NET_ERROR || join_wait > 9) { pending_join = false; status("COULD NOT REACH THE RELAY. CHECK YOUR CONNECTION."); }
  }
  if (L.in_lobby && st == NET_ONLINE) {
    L.hb += dt;
    if (L.hb >= 1.5) {
      L.hb = 0; beat();
      double A = now_ms(); bool P = false;
      for (int i = 0; i < L.npeers; i++) if (A - L.peers[i].seen > 1e4) { L.peers[i--] = L.peers[--L.npeers]; P = true; }
      if (P) fire_peers();
    }
  }
  if (L.pending && now_ms() - L.pend_at > 15e3) { L.pending = false; status("NO ANSWER"); }
  if (L.chal && now_ms() - L.ch_at > 14e3) { L.chal = false; ui_online.challenge = false; }
#ifdef CK_ONLINE_TEST
  if (L.chal && now_ms() - L.ch_at > 1000) { ck_log("online: accepting %s", L.ch_n); online_accept(); }
#endif
  if (Z.on) {
    Z.st_timer += dt;
    if (Z.st_timer >= 0.7) {
      Z.st_timer = 0;
      if (clasp_status() == NET_ERROR) snprintf(ui_pads.st, sizeof ui_pads.st, "COULD NOT REACH THE RELAY.");
      else if (clasp_status() == NET_ONLINE) pad_status();
    }
  }
}

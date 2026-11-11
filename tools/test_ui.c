/* test_ui.c: a test driver for the UI port (ui.c, ui_screens.c). It stands in
 * for game.c: a flat backdrop (the page's body colour), and the screens and
 * HUD states on a frame schedule the rig captures, matching the states the
 * Chrome dumps of the original were taken in. After the schedule the last
 * screen stays up and the keys drive it.
 *
 *   OUT=build-ui CK_SRCS="src/ck.c src/main.c src/input.c src/overlay.c src/fx.c src/stubs.c
 *     src/ui.c src/ui_screens.c tools/test_ui.c" ./build.sh
 *   node tools/shot.mjs build-ui/coffee-and-kill.wasc out/ui 50,110,170,...   */
#include "game.h"
#include "ui.h"
#include "ck.h"
#include "input.h"
#include <string.h>
#include <stdio.h>

static ck_sel sel = {{{"moheeb", "pipe"}, {"luis", "sledge"}}, 1};
static int frame;
static char last_act[24];

static void on_act(const char *a) { snprintf(last_act, sizeof last_act, "%s", a); ck_log("act %s", a); ui_toast(a); }
static void on_weapon(int s, const char *w) { snprintf(sel.slots[s].weapon, sizeof sel.slots[s].weapon, "%s", w); }
static void on_swap(void) { char t[8]; strcpy(t, sel.slots[0].ch); strcpy(sel.slots[0].ch, sel.slots[1].ch); strcpy(sel.slots[1].ch, t); }
static void on_diff(int l) { sel.level = l; }

extern bool ui_test_no_grain;
void game_init(void) {
  ui_init();
  ui_test_no_grain = true;
  ui_cb.act = on_act; ui_cb.weapon = on_weapon; ui_cb.swap = on_swap; ui_cb.diff = on_diff;
  ui_show("s-load");
  ui_load(0.42f, "building the hackerspace");
}
bool game_boot(int stage) { (void)stage; return true; }

static void online_fill(void) {
  ui_online_t *o = &ui_online;
  memset(o, 0, sizeof *o);
  strcpy(o->name, "PLAYER42"); strcpy(o->room, "LOBBY");
  strcpy(o->status, "IN THE MAIN LOBBY. ANYONE WHO OPENS ONLINE LANDS HERE. CHALLENGE A PLAYER BELOW.");
  o->link_row = true; strcpy(o->link, "https://weird.link/coffeekill?room=LOBBY");
  strcpy(o->weapon, "pipe");
  o->npeers = 2;
  strcpy(o->peers[0].id, "a1"); strcpy(o->peers[0].n, "LUIS"); strcpy(o->peers[0].w, "sledge");
  strcpy(o->peers[1].id, "b2"); strcpy(o->peers[1].n, "ZED"); strcpy(o->peers[1].w, "crowbar"); o->peers[1].busy = true;
  o->challenge = true; strcpy(o->chtxt, "LUIS CHALLENGES YOU");
}

/* the schedule: each state starts at a multiple of 60 frames; the rig shoots at +50 */
static void state(int k) {
  switch (k) {
    case 0: break;                                        /* load */
    case 1: ui_show("s-title"); break;
    case 2: ui_show("s-title"); ui_key(WC_KEY_DOWN); ui_key(WC_KEY_DOWN); break;   /* menuIdx 2 */
    case 3: ui_set_select(&sel, 1); ui_show("s-select"); break;
    case 4: ui_set_select(&sel, 1); ui_show("s-select"); ui_key(WC_KEY_DOWN); break;   /* focus w0 */
    case 5: ui_set_select(&sel, 2); ui_show("s-select"); break;
    case 6: ui_show("s-controls"); break;
    case 7: ui_show("s-pause"); break;
    case 8: ui_set_end("MOHEEB", true, "THE SHOP IS YOURS"); ui_show("s-end"); break;
    case 9: online_fill(); ui_show("s-online"); break;
    case 10:
      strcpy(ui_pads.code, "KQTR");
      strcpy(ui_pads.url, "https://heatsync.sfo3.cdn.digitaloceanspaces.com/coffee-and-kill.html?pad=KQTR");
      strcpy(ui_pads.st, "P1 WAITING   P2 WAITING   (ADD &p=2 FOR PLAYER 2)");
      ui_show("s-pads"); break;
    case 11:
      ui_hide_all(); ui_hud(true);
      ui_set_names("MOHEEB", "LUIS", "P1", "CPU");
      ui_set_health(0, 72, true); ui_set_health(1, 45, true);
      ui_set_meter(0, 60); ui_set_meter(1, 100);
      ui_set_pips(0, 1, 2); ui_set_pips(1, 0, 2);
      ui_set_timer(37.2f); ui_set_round("ROUND 2");
      ui_combo(0, 3, "NICE"); ui_combo(1, 5, "ABSOLUTELY EXTREME");
      ui_set_hint("WASD move   Q light   E heavy   R kick   SPACE block   SHIFT dodge   F extreme");
      ui_announce("ROUND 2", "FINAL ROUND", true);
      ui_toast("DANCE GRADE A  EXTREME +60");
      break;
    case 12:   /* the moving parts: a pop announce, hits, the low timer, a flash */
      ui_announce("K.O.", "PERFECT", false);
      ui_set_health(1, 10, false); ui_set_timer(8);
      ui_fx("14", 500, 300, ""); ui_fx("21", 700, 330, "big"); ui_fx("BLOCKED", 300, 420, "blk"); ui_fx("DODGED", 900, 420, "dg");
      ui_flash(0xffffff, 0.6f, 300);
      break;
    case 13: ui_announce("DANCE BATTLE", "HIT THE ARROWS", true); break;
  }
}

void game_frame(float dt) {
  if (frame % 60 == 0 && frame / 60 <= 13) state(frame / 60);
  if (frame / 60 == 11 && frame % 60 == 30) ui_toast("DANCE GRADE A  EXTREME +60");
  frame++;
  ui_frame(dt);
}
void game_draw(void) {
  render_clear(0x14110d);
  ui_draw();
}

/* ui.h: the page's DOM: the HUD, every screen (#screens), the toast, the
 * floating hit numbers, the flash, the load screen and the online and phone
 * pad screens. The Rr class of the page, method for method, drawn with cv.h
 * into overlay textures and laid out from the page's CSS at a 1280 x 720
 * viewport (clamp() and vw/vh resolved there; the max-height:820px media
 * query applies at 720).
 *
 * Draw order (z-index): #vignette 2, #hud 4, #screens 5, #flash 6. The page's film grain
 * (#grain, z 3) is left out: the cart renders clean.
 * ui_draw() draws all of those; the dance canvas (z 25) is dance.c's. */
#ifndef CK_UI_H
#define CK_UI_H
#include <stdbool.h>
#include <stdint.h>

/* P.sel: the two fighter slots, the CPU level and the stage */
typedef struct {
  struct { char ch[12]; char weapon[12]; int colour; } slots[2];  /* roster and weapon keys (roster.h, weapons.h), colour 0..3 */
  int level;                                          /* 0 ROOKIE, 1 JOURNEYMAN, 2 EXTREME */
  int stage;                                          /* stage.h index */
} ck_sel;

/* the callbacks the app sets (A.cb) */
typedef struct {
  void (*act)(const char *act);            /* data-act: "1p" "2p" "online" "start" "back" "resume" ...;
                                            * this cart's: "tourney" "stage" "stageback" "tstart" "tplay"
                                            * "tskip" "tquit" "tnew" */
  void (*weapon)(int slot, const char *w);
  void (*swap)(void);
  void (*diff)(int level);
  void (*fighter)(int slot, const char *key);
  void (*stage)(int stage);
  /* the fighter select: what slot shows changed (in ck_sel); now: load its body at once (a
   * confirm), else once the cursor has rested on it */
  void (*pick)(int slot, bool now);
  void (*cheer)(int slot);                 /* the slot's fighter plays its victory clip once */
} ui_callbacks;
extern ui_callbacks ui_cb;

void ui_init(void);
void ui_frame(float dt);          /* CSS transitions and animations, timers (toast, fx) */
void ui_draw(void);               /* vignette, HUD, screens, flash */

/* the current screen ("s-title", "s-select", "s-controls", "s-pause", "s-end",
 * "s-online", "s-pads", "s-load"), or NULL after hideAll */
const char *ui_screen(void);
void ui_show(const char *screen);
void ui_hide_all(void);
/* Rr.onKey: a keydown (WC_KEY_* / CK_KEY_* code), not a repeat; true when the shown screen used it */
bool ui_key(int code);
/* whether the screen used this frame's key event i (input_key_events order) */
bool ui_key_used(int i);
/* the mouse: hover selects menu buttons, a click acts (the page's click and mouseover handlers) */
void ui_pointer(float x, float y, bool pressed);

void ui_set_select(ck_sel *sel, int mode);
/* the select shown again from the stage select: the last step taken is undone, the rest kept */
void ui_select_return(void);
/* the screen up reads the pads by player itself (input.h menu events): the game pushes no keys */
bool ui_reads_pads(void);
/* the tourney the setup, bracket and champion screens show (tourney.h) */
void ui_set_tourney(void *tourney);
/* repaint whichever of this cart's menu screens is up (after the game changed what it shows) */
void ui_menus_changed(void);
void ui_set_names(const char *n0, const char *n1, const char *pn0, const char *pn1);  /* pn NULL: leave P1 / P2 */
void ui_set_health(int i, float pct, bool instant);
void ui_set_meter(int i, float pct);
void ui_set_pips(int i, int on, int of);
void ui_set_timer(float t);
void ui_set_round(const char *s);
void ui_set_hint(const char *s);           /* may name buttons with ui_hint.h's glyphs: "{X} LIGHT" */
void ui_set_extreme_key(int i, const char *glyph);   /* the button by a full EXTREME meter ("{RT}", "[F]", "") */
void ui_combo(int i, int n, const char *small);
void ui_announce(const char *big, const char *small, bool stay);
void ui_clear_announce(void);
void ui_toast(const char *s);
/* a floating number or word at a screen point; cls "", "big", "blk", "dg" */
void ui_fx(const char *text, float x, float y, const char *cls);
void ui_flash(uint32_t rgb, float a, float ms);
void ui_hud(bool on);
/* the end card: name is "MOHEEB" with wins true for "<name> <em>WINS</em>", or "DRAW" */
void ui_set_end(const char *name, bool wins, const char *sub);
void ui_load(float p, const char *msg);
/* the loading card for a match: the selection's two fighters (ui_set_select) face to face, with
 * their tags (P1, CPU) and the stage's name; ui_load moves its bar. Until the next ui_show. */
void ui_load_vs(const char *stage, const char *tag0, const char *tag1);
void ui_load_error(const char *msg);

/* #s-online: the state online.c shows (fields are edited here, by ui_key and input_text) */
typedef struct {
  char name[16], room[20], link[160], status[160];
  bool link_row;                   /* #ol-linkrow shown */
  char weapon[12];                 /* the weapon chips, the one that is on */
  char fighter[12], stage[12];     /* this cart's picks (cart against cart only) */
  int npeers;
  struct { char id[16], n[16], w[12], c[12]; int v; bool busy; } peers[16];   /* c, v: a cart's fighter and wire version */
  bool challenge; char chtxt[48];  /* #ol-ch */
  int focus;                       /* 0 none, 1 name, 2 room */
} ui_online_t;
extern ui_online_t ui_online;
/* online.c's callbacks: a weapon chip, a CHALLENGE button, ENTER in the room field */
extern void (*ui_on_olweapon)(const char *w);
extern void (*ui_on_olfighter)(const char *key);
extern void (*ui_on_olstage)(const char *key);
extern void (*ui_on_challenge)(const char *peer_id);
extern void (*ui_on_room_enter)(void);

/* #s-pads */
typedef struct {
  char code[8], url[160], st[160];
  int qr_n; uint8_t qr[177 * 177]; /* the QR modules (1 dark), qr_n x qr_n */
} ui_pads_t;
extern ui_pads_t ui_pads;

/* the screen-space point of a world point is the game's; ui_fx takes pixels */
#endif

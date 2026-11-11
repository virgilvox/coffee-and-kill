/* input.h: the page's Input (qe) and its keyboard, over wasmcart pads, keys
 * and pointers.
 *
 * Keyboard codes are wasmcart's (USB HID usages, WC_KEY_* in wasmcart.h) plus
 * the modifiers below; the page's KeyboardEvent.code names map to them one
 * to one (KeyQ = WC_KEY_Q, ShiftLeft = CK_KEY_LSHIFT, Comma = WC_KEY_COMMA). */
#ifndef CK_INPUT_H
#define CK_INPUT_H
#include <stdbool.h>
#include <stdint.h>
#include "wasmcart.h"

#define CK_KEY_LCTRL  0xE0
#define CK_KEY_LSHIFT 0xE1
#define CK_KEY_LALT   0xE2
#define CK_KEY_RCTRL  0xE4
#define CK_KEY_RSHIFT 0xE5
#define CK_KEY_RALT   0xE6
#define CK_KEY_START  0xF0   /* a pad's Start on a menu (game.c pushes it): Enter, unless the screen takes it */

/* qe.poll(s): one player's intent this frame. Movement is held, the five
 * attacks are presses (edges), block is held. */
typedef struct {
  float mx, my;
  bool block, light, heavy, kick, dodge, special;
} intent_t;

void input_init(void);
void input_frame(const wc_pad_t *pads, const uint8_t *keys, const wc_pointer_t *ptr);
void input_end_frame(void);             /* qe.endFrame(): forget this frame's presses */

extern int input_mode;                  /* qe.mode: 1 = one player (both pads and every key drive P1), 2 = two */
intent_t input_poll(int slot);
/* qe.remote: phone pads (online.c) add their intent to a slot; NULL = none */
extern bool (*input_remote)(int slot, intent_t *out);

bool input_key_down(int code);          /* _r: held */
bool input_key_pressed(int code);       /* O8: went down this frame */
bool input_any_key(void);

/* keydown events this frame, in order (no repeats): the keyboard's, plus the
 * pads' menu presses translated by the game (game.c pushes them here) */
int input_key_events(const int **codes);
void input_push_key(int code);
/* the typed characters this frame (for the online screen's two text fields) */
int input_text(const char **utf8);

/* pad edges for the game's own mapping (menus, pause) */
bool input_pad_pressed(int pad, uint32_t btn);
bool input_pad_down(int pad, uint32_t btn);
bool input_pad_connected(int pad);

/* the mouse (wasmcart pointer 0), in CSS pixels: the primary button (down, pressed, released),
 * the secondary (rdown, rpressed) and whether it moved this frame */
typedef struct { bool active; float x, y; bool down, pressed, released, rdown, rpressed, moved; } ck_pointer;
const ck_pointer *input_pointer(void);

/* the device a person last used, so prompts can name the right buttons: a key, a pad, the
 * mouse or a phone pad. A pad at boot counts as used (the station has no keyboard). */
enum { DEV_KEYS, DEV_PAD, DEV_MOUSE, DEV_PHONE };
extern int input_last_dev;
void input_note_dev(int dev);
/* the device each side last fought with (input_poll sees it), from input_dev_reset at a match's start */
int input_dev_of(int slot);
void input_dev_reset(void);

/* menu events by player, from the pads and the phone pads (the keyboard's are the screen's to
 * read, since its keys depend on the screen and the mode). Pad 0 is player 0 and pad 1 player
 * 1; in one player mode every pad is player 0. A held direction repeats. */
enum { ME_UP, ME_DOWN, ME_LEFT, ME_RIGHT, ME_OK, ME_BACK, ME_SWAP, ME_PREV, ME_NEXT, ME_START, ME_N };
typedef struct { int player, ev, dev; } menu_ev;
int input_menu_events(const menu_ev **evs);
void input_push_menu(int player, int ev, int dev);   /* a phone pad's (online.c) */
/* how long the back button (B) has been held on any pad, seconds (0: not held) */
float input_back_held(void);

#endif

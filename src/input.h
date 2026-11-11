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
/* a stick or d-pad direction that went past the menu threshold this frame:
 * -1 / +1 on x and y (y +1 = down), 0 = none */
void input_pad_nav(int pad, int *dx, int *dy);

/* the mouse (wasmcart pointer 0), in CSS pixels */
typedef struct { bool active; float x, y; bool down, pressed, released; } ck_pointer;
const ck_pointer *input_pointer(void);

#endif

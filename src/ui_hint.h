/* ui_hint.h: prompts that name the buttons of the device in use (input.h input_last_dev). A
 * prompt is a line of text with glyphs in it:
 *
 *   {A} {B} {X} {Y}          a pad's face buttons, round, in the web shell's .pb colours
 *   {LB} {RB} {LT} {RT} {START}   a pad's other buttons, as pills
 *   [ENTER] [ESC] [W]        a key, as the controls screen draws one: an ink box, cream letters
 *   <CLICK> <LIGHT>          the mouse and the phone pad's buttons: a paper box, ink letters
 *
 * so "{A} PICK  {B} BACK" or "[ENTER] PICK  [ESC] BACK". hint_pick() chooses one of four
 * prompts by the device last used. */
#ifndef CK_UI_HINT_H
#define CK_UI_HINT_H
#include "ui_int.h"

typedef struct { float fs, ls; col_t text; const tshadow *sh; } hint_style;

float hint_width(const hint_style *st, const char *s);
/* paints s from x with its middle on the line cy */
void hint_paint(cv *c, const hint_style *st, float x, float cy, const char *s);
/* the glyph height for a text size (a row of prompts is this tall) */
float hint_h(float fs);

/* the prompt for the device last used: pad, keyboard, mouse, phone pad (NULL: the pad's) */
const char *hint_pick(const char *pad, const char *keys, const char *mouse, const char *phone);

/* a hint line in its own layer, repainted when its text changes; drawn centred on cx */
typedef struct { layer L; char s[200]; float w, h; hint_style st; } hint_line;
void hint_line_set(hint_line *h, const hint_style *st, const char *s);
void hint_line_draw(hint_line *h, float cx, float cy, float alpha);

#endif

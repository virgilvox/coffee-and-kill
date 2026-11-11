/* ui_kit.h: the pieces ui_screens.c (the page's screens) and ui_menus.c (the screens this
 * cart adds) share: partial canvases, the .btn, the .chip and the label, hit rects, the shades
 * and the BACK button. All of them follow the page's CSS (see ui_screens.c). */
#ifndef CK_UI_KIT_H
#define CK_UI_KIT_H
#include "ui_int.h"

/* a canvas covering only part of the page: its page origin, set as the canvas transform */
typedef struct { layer L; int ox, oy; } rlayer;
void R_begin(rlayer *r, int x, int y, int w, int h);
void R_draw(rlayer *r);

/* a .btn: normal (ink, cream text, 6 6 pink), .sel / :hover (pink, ink text,
 * translate(-3,-3), 9 9 ink); .ghost (cream, ink, 5 5 ink; selected: yellow,
 * translate(-3,-3), the shadow stays 5 5 ink); .go (pink, ink, 7 7 ink; selected:
 * translate(-3,-3)). transition: transform, box-shadow, background, color .08s */
enum { BTN_NORMAL, BTN_GHOST, BTN_GO };
typedef struct {
  float x, y, w, h, fs, padx, pady; int kind; bool center;
  char label[40]; char act[16];
  bool sel; trans_t t; float painted; layer L;
} btn_t;
void btn_init(btn_t *b, float x, float y, float w, float h, float fs, float padx, float pady, int kind, const char *label, const char *act);
void btn_set_sel(btn_t *b, bool sel);
void btn_paint_at(cv *c, const btn_t *b, float s, float ox, float oy);   /* into a canvas painted whole */
void btn_draw(btn_t *b);                                                  /* its own layer, animated */
bool btn_hit(const btn_t *b, float x, float y);

/* hit rects of a painted screen */
typedef struct { float x, y, w, h; int kind; int a, b; char s[20]; } hit_t;
enum { H_ACT, H_WEAPON, H_SWAP, H_DIFF, H_FIELD, H_OLW, H_CH, H_FIGHTER, H_STAGE, H_TCTL, H_TFIGHTER, H_TWEAPON, H_TSIZE, H_TSTAGE, H_OLF, H_OLS };
extern hit_t hits[64]; extern int nhits;
void hit_add(float x, float y, float w, float h, int kind, int a, int b, const char *s);

void draw_shade_h(void);    /* the title's gradient from the left */
void draw_shade_v(void);    /* the select's gradient from the bottom */
/* .chip: type 14, ls .16em, border 3, padding 5 10, paper / .on ink with cream text */
float chip_w(const char *s);
void chip_paint(cv *c, float x, float y, const char *s, bool on);
void lab_paint(cv *c, float x, float top, const char *s);   /* .lab: type 13, ls .2em */
void focus_ring(cv *c, float x, float y, float w, float h);  /* 3px dashed pink, offset 2 */
extern btn_t back_b;        /* .btn.ghost.back at (20, 20) */
void draw_back(void);
extern bool kbd_nav;        /* the keyboard moved the focus (show the ring) */

/* ui_select.c: the fighter select, and its grid as a modal for the tourney and online screens */
void select_init(void);
void select_show(void);
void select_frame(float dt);
void select_draw(void);
void select_key(int code);
void select_pointer(float x, float y, bool pressed);
void select_modal_open(int fighter, void (*done)(int fighter));
bool select_modal_active(void);
void select_modal_close(void);
void select_face(cv *c, int fighter, float x, float y, float w, float h);   /* a fighter's portrait */

/* ui_menus.c: the select, stage and tourney screens */
void menus_init(void);
bool menus_owns(const char *id);
void menus_show(const char *id);
void menus_draw(void);
void menus_key(int code);
void menus_pointer(float x, float y, bool pressed);
void menus_frame(float dt);

#endif

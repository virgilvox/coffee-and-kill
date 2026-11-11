/* ui_int.h: what ui.c (HUD, state, input) and ui_screens.c (the screens)
 * share: the text engine, the CSS box helpers, canvas layers and easing.
 *
 * Text follows Chrome's layout for these three fonts: a line box's content
 * area is round(ascent * size) + round(descent * size) (hhea metrics), the
 * glyphs sit floor((line-height - that) / 2) below the line box top, every
 * glyph advances by its hmtx width plus letter-spacing, and Archivo kerns
 * (its GPOS kern pairs, ui_kern.h). */
#ifndef CK_UI_INT_H
#define CK_UI_INT_H
#include <stdbool.h>
#include <stdint.h>
#include "cv.h"
#include "overlay.h"
#include "ui.h"

/* colours as 0xRRGGBB plus an alpha */
typedef struct { uint32_t rgb; float a; } col_t;
#define COL(h) ((col_t){(h), 1.f})
#define COLA(h, a) ((col_t){(h), (a)})

/* text-shadow: up to 5 hard shadows, first on top */
typedef struct { int n; struct { float dx, dy; col_t c; } s[5]; } tshadow;

/* ---- text ---- */
float t_asc(int font, float fs);              /* round(ascent * fs) */
float t_desc(int font, float fs);             /* round(descent * fs) */
float t_line(int font, float fs);             /* line-height normal */
float t_width(int font, float fs, float ls, const char *s);
/* glyphs of s from x with the baseline at y (left aligned), in colour */
void t_draw(cv *c, int font, float fs, float ls, float x, float base, const char *s, col_t col);
void t_draw_sh(cv *c, int font, float fs, float ls, float x, float base, const char *s, col_t col, const tshadow *sh);
/* the baseline of a line box at top with line-height lh */
float t_base(int font, float fs, float top, float lh);
/* word wrap s into width w (break_all: anywhere); returns the line count,
 * each line a copy in lines[i] (max 8 lines of 160 bytes) */
int t_wrap(int font, float fs, float ls, const char *s, float w, bool break_all, char lines[][160], int max);
/* white-space collapse: runs of spaces to one, trim */
void t_collapse(char *dst, const char *src, int cap);

/* ---- CSS boxes (in the canvas's current transform) ---- */
void c_rect(cv *c, float x, float y, float w, float h, col_t col);
void c_border(cv *c, float x, float y, float w, float h, float bw, col_t col);
/* the whole box: box-shadow (hard), background, border */
void c_box(cv *c, float x, float y, float w, float h, col_t bg, float bw, col_t bc, float sdx, float sdy, col_t sc);
/* a dashed line along a border edge (Chrome's dash pattern for that width) */
void c_dash_h(cv *c, float x, float y, float w, float bw, col_t col);
void c_dash_rect(cv *c, float x, float y, float w, float h, float bw, col_t col);   /* a dashed outline */

/* ---- layers: a canvas and its texture ---- */
typedef struct { cv *c; ov_tex *t; int w, h; bool dirty; } layer;
void L_ensure(layer *l, int w, int h);        /* (re)make at that size, cleared and dirty */
void L_upload(layer *l);                      /* upload if dirty */

/* ---- easing (CSS timing functions) ---- */
float ease_bezier(float x1, float y1, float x2, float y2, float t);
#define EASE(t) ease_bezier(.25f, .1f, .25f, 1.f, (t))
#define EASE_OUT(t) ease_bezier(0.f, 0.f, .58f, 1.f, (t))
/* a CSS transition of one number */
typedef struct { float from, to, start, dur, delay; bool init; } trans_t;
void tr_set(trans_t *tr, float v, float now, float dur, float delay);   /* retarget from the current value */
void tr_jump(trans_t *tr, float v);
float tr_get(const trans_t *tr, float now);
bool tr_busy(const trans_t *tr, float now);

col_t col_mix(col_t a, col_t b, float t);

/* the HSL mark (the kicker's svg), rasterised once */
extern uint8_t ui_mark_px[];   /* MARK_N x MARK_N RGBA */
#define MARK_N 34
void ui_mark_init(void);

/* ---- the screens (ui_screens.c) ---- */
extern float ui_now;                          /* seconds, the CSS clock */
void scr_init(void);
void scr_show(const char *id);                /* repaint for a newly shown screen */
void scr_frame(void);
void scr_draw(void);                          /* the shown screen */
void scr_key(int code);                       /* Rr.onKey (when a screen is up) */
void scr_pointer(float x, float y, bool pressed);
void scr_online_changed(void);
void scr_pads_changed(void);
void scr_end_changed(const char *name, bool wins, const char *sub);
void scr_load(float p, const char *msg, bool err);
extern const char *scr_cur;                   /* the shown screen id, NULL = none */
extern ck_sel *scr_sel; extern int scr_mode;
extern int scr_menu_idx;

#endif

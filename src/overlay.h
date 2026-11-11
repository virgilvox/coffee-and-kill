/* overlay.h: the page's DOM layers drawn over the 3D view with GL, blended in
 * sRGB space (as the browser composites): textured quads from canvases, solid
 * fills, the vignette and the flash. Coordinates are CSS
 * pixels of the 1280 x 720 viewport, y down. */
#ifndef CK_OVERLAY_H
#define CK_OVERLAY_H
#include <stdint.h>
#include <stdbool.h>
#include "cv.h"

typedef struct ov_tex { unsigned id; int w, h; } ov_tex;

void ov_init(void);
ov_tex *ov_tex_new(int w, int h);
void ov_tex_free(ov_tex *t);
/* upload a canvas (its whole area, or a sub rect at the same place) */
void ov_tex_from_cv(ov_tex *t, cv *c);
void ov_tex_from_cv_rect(ov_tex *t, cv *c, int x, int y, int w, int h);

/* begin / end a run of overlay draws into the bound framebuffer (CK_W x CK_H) */
void ov_begin(void);
void ov_end(void);
/* draw t through the affine m (CSS matrix(a, b, c, d, e, f): texture pixel (u, v)
 * lands at (a u + c v + e, b u + d v + f)), with opacity and a brightness factor */
void ov_draw_m(ov_tex *t, const float m[6], float alpha, float bright);
void ov_draw(ov_tex *t, float x, float y, float alpha);
/* a sub rect of t (sx, sy, sw, sh) to (x, y) at scale 1 */
void ov_draw_sub(ov_tex *t, int sx, int sy, int sw, int sh, float x, float y, float alpha);
void ov_fill(float x, float y, float w, float h, uint32_t rgb, float a);
void ov_vignette(void);

/* CSS transform helpers: m = translate(tx, ty) rotate(r) scale(s) about the
 * point (ox, oy) of the element whose box starts at (x, y) */
void ov_matrix(float m[6], float x, float y, float ox, float oy, float rot, float sx, float sy, float tx, float ty);
void ov_matrix_skew(float m[6], float x, float y, float ox, float oy, float skew_x);

#endif

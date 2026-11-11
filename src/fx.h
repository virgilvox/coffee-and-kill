/* fx.h: the effects pass. What the page draws with ShaderMaterial, Points,
 * Sprites and additive MeshBasicMaterials goes through one small GL program
 * here, after three.c's scene, into the same framebuffer, depth tested
 * against the scene and blended on encoded colour, as WebGL blends the page.
 *
 * The program reproduces what three's material would output:
 *   colour = vertex colour * uniform colour * texture        (as the material)
 *   FXF_FOG      mix toward the scene's FogExp2 colour (three's fog chunk)
 *   FXF_TONEMAP  ACESFilmic at the renderer's exposure (toneMapped: true)
 *   FXF_ENCODE   linear to sRGB (a three material's output; a ShaderMaterial
 *                that writes gl_FragColor raw does not, so leave it off)
 *   blending     FXB_NORMAL: SRC_ALPHA, ONE_MINUS_SRC_ALPHA (alpha ONE,
 *                ONE_MINUS_SRC_ALPHA); FXB_ADD: SRC_ALPHA, ONE
 * Colours given to the draws are in the space the original's maths ran in:
 * linear for three materials (hex colours convert with fx_hex_linear), raw
 * for the ShaderMaterials. */
#ifndef CK_FX_H
#define CK_FX_H
#include <stdbool.h>
#include <stdint.h>
#include "three.h"

enum { FXB_NORMAL = 1, FXB_ADD = 2 };
enum {
  FXF_FOG = 1, FXF_TONEMAP = 2, FXF_ENCODE = 4,
  FXF_DEPTH_TEST = 8, FXF_DEPTH_WRITE = 16,
  FXF_TEX_LINEAR = 32,   /* the texture holds data (NoColorSpace): sample raw; else it is sRGB and decoded */
};
/* point shapes */
enum { FXP_SQUARE = 0, FXP_TEX = 1, FXP_SOFT_DISC = 2 };

void fx_init(void);
/* a texture from RGBA8 (unpremultiplied); repeat wraps, mip builds mipmaps */
unsigned fx_texture(const uint8_t *rgba, int w, int h, bool repeat, bool mip);
void fx_hex_linear(uint32_t hex, float out[3]);   /* Color.setHex: sRGB hex to linear */
void fx_hex_raw(uint32_t hex, float out[3]);      /* the hex as 0..1 values, no conversion */

/* once a frame, before any draw: the camera, the viewport height and the fog */
void fx_frame(t3_camera *cam, float fog_density, uint32_t fog_hex, float exposure);
void fx_begin(void);
void fx_end(void);

/* triangles: xyz (n*3), uv (n*2 or NULL), rgba vertex colours (n*4 or NULL);
 * model NULL = identity; col is the material colour and opacity (rgb, a) */
void fx_tris(const float *xyz, const float *uv, const float *rgba, int n, const float *model,
             unsigned tex, const float col[4], int blend, int flags, bool two_sided);
/* points: xyz (n*3), rgba (n*4 or NULL), size (n or NULL = size0); the
 * drawn diameter in pixels is size * scale / -z_view (PointsMaterial passes
 * scale = viewport height / 2; the page's particle shader its own uScale) */
void fx_points(const float *xyz, const float *rgba, const float *size, int n, float size0, float scale,
               int shape, unsigned tex, const float col[4], int blend, int flags);
/* a Sprite: centre, world size, rotation, the texture, colour and opacity */
void fx_sprite(const float pos[3], float size, float rot, unsigned tex, const float col[4], int blend, int flags);

/* the camera of the frame (for billboards and screen projection) */
void fx_project(const float world[3], float *sx, float *sy);   /* CSS pixels */
const float *fx_view(void);
const float *fx_proj(void);

#endif

/* geom.h: the three.js geometries three.c lacks, ported from r186: TubeGeometry
 * over a CatmullRomCurve3 (Frenet frames, arc length spacing) and
 * ExtrudeGeometry of one closed polygon shape (earcut caps, bevel layers, the
 * WorldUVGenerator UVs and the two groups), both vertex for vertex. */
#ifndef CK_GEOM_H
#define CK_GEOM_H
#include <stdbool.h>
#include "three.h"

/* new TubeGeometry(new CatmullRomCurve3(pts, closed_curve, type, tension),
 * tubular_segments, radius, radial_segments, closed); centripetal 1 picks
 * "centripetal", 0 "catmullrom" (tension then matters) */
t3_geometry *geom_tube(const t3_vec3 *pts, int n, bool closed_curve, int centripetal, float tension,
                       int tubular_segments, float radius, int radial_segments, bool closed);

/* new ExtrudeGeometry(new Shape(xy polygon), { depth, bevelEnabled,
 * bevelThickness, bevelSize, bevelSegments }) (steps 1, bevelOffset 0); xy
 * holds n points (x, y), not repeating the first */
t3_geometry *geom_extrude(const float *xy, int n, float depth, bool bevel, float bevel_thickness, float bevel_size,
                          int bevel_segments);

#endif

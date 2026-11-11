/* arena.h: the hackerspace (Z0 of the page): the room, its painted canvas
 * textures, the Kenney props, the coffee bar, the Arduino Mega wall, the
 * robot eye, the screens, the beacons, the cables, the lights and the dust,
 * with the updaters that animate them, plus the scene setup of the game's
 * constructor (background, fog, the RoomEnvironment PMREM). */
#ifndef CK_ARENA_H
#define CK_ARENA_H
#include "three.h"

/* the room's half extents (RA, fP, MP) and the ring radius */
#define ROOM_HX 13.0f
#define ROOM_HZ 10.0f
#define ROOM_HY 8.2f

/* scene.background 723983, FogExp2(1447965, 0.011), environment from
 * PMREMGenerator.fromScene(new RoomEnvironment(), 0.04), intensity 0.45 */
void arena_scene_setup(t3_renderer *r, t3_scene *scene);
/* builds the arena into the scene; staged so the load screen can move:
 * returns 1 when done (stage 0, 1, 2 ...) */
int arena_build(t3_scene *scene, int stage);
/* arena.update(dt, t): every updater, the dust drift, the work lamps' flicker */
void arena_update(float dt, float t);
/* arena.focus: the robot eye looks at it (the fighters' midpoint, y 1.2) */
void arena_set_focus(float x, float y, float z);

/* the additive and transparent effects the arena owns (the light cones of
 * the windows, the beacon beams, the dust, the sparks) are drawn with fx.h,
 * between fx_begin and fx_end, after the scene */
void arena_fx_draw(void);

#endif

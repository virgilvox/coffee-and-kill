/* game.h: the game (wv of the page) and the app glue around it (the page's
 * startup function: callbacks, keys, the online lobby). main.c drives it. */
#ifndef CK_GAME_H
#define CK_GAME_H
#include <stdbool.h>
#include <stdint.h>
#include "three.h"

void game_init(void);
/* the loading screen's stages (init(A) of the page): true when done */
bool game_boot(int stage);
void game_frame(float dt);
void game_draw(void);

/* main.c's render pipeline */
extern t3_renderer *CK_R;
void render_scene(t3_scene *scene, t3_camera *cam);
void render_clear(uint32_t rgb);

/* the page's localStorage (ck_name, ck_weapon), in the cart's save */
const char *ck_save_get(const char *key, const char *def);
void ck_save_set(const char *key, const char *val);

#endif

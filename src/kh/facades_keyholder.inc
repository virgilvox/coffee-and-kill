/* facades_keyholder.inc: storefronts bikemesa does not have, built from the street as it stands,
 * in the same style, and the street furniture. The sedan follows bikemesa's city.inc (MIT, see
 * facades_mesa.inc), with fewer segments. */

/* The front of the lab that the model does not hold: columns and timber pergola over the sidewalk,
 * white parapet with the small sign. The origin is the door on the street line; the lab spans x -2.8 to 6.6. */
static void fk_lab_front(t3_object *r, const whimsy_parts *p, t3_material *white, t3_material *timber) {
  const float xs[] = {-2.3f, 1.7f, 4.9f};
  for (int i = 0; i < 3; i++) {
    prop(r, p->cyl, white, xs[i], 1.7f, 3.0f, .27f, 3.4f, .27f);
    prop(r, p->box, white, xs[i], 3.42f, 3.0f, .75f, .22f, .75f);
    prop(r, p->box, white, xs[i], .1f, 3.0f, .7f, .2f, .7f);
  }
  /* the lintel, and beams running out from the wall, painted white with timber slats on top */
  prop(r, p->box, white, 1.9f, 3.65f, 3.0f, 9.6f, .22f, .34f);
  prop(r, p->box, white, 1.9f, 3.65f, .1f, 9.6f, .22f, .3f);
  for (int i = 0; i <= 8; i++) prop(r, p->box, white, -2.6f + i * 1.14f, 3.8f, 1.7f, .16f, .16f, 3.9f);
  for (int i = 0; i <= 14; i++) prop(r, p->box, timber, 1.9f, 3.92f, .05f + i * .25f, 9.8f, .09f, .09f);
  /* parapet panel with the sign */
  prop(r, p->box, white, 1.9f, 4.45f, .1f, 9.4f, 1.3f, .4f);
  prop(r, p->box, p->cream, 1.9f, 5.14f, .1f, 9.5f, .08f, .5f);
  mesa_logo(r, "heatsync.png", -.4f, 4.55f, .32f, .62f, .62f);
  mesa_name(r, "HeatSync Labs", 1.5f, 4.55f, .32f, 2.9f, .34f);
}
/* the banner on the lamp by the door */
static void fk_trellis(t3_object *r, const whimsy_parts *p) {
  /* the corner arcade east of the lab: arched windows behind round columns, the pergola carried on. w = 11.3 */
  t3_material *white = toy_paint(.9f, .9f, .86f), *timber = toy_paint(.74f, .6f, .44f), *glass = toy_paint(.17f, .25f, .3f);
  prop(r, p->box, white, 0, 2.4f, -1.5f, 11.4f, 4.8f, 3);
  for (int i = 0; i < 2; i++) {
    float x = -2.8f + i * 5.6f;
    prop(r, p->box, glass, x, 1.65f, .02f, 3.6f, 2.5f, .06f);
    prop(r, p->ball, glass, x, 2.9f, .02f, 1.8f, .85f, .05f);
    prop(r, p->box, p->black, x, .33f, .06f, 3.8f, .66f, .08f);
    for (int k = -1; k <= 1; k++) prop(r, p->box, p->black, x + k * 1.2f, 1.65f, .07f, .05f, 2.5f, .03f);
  }
  for (int i = 0; i < 4; i++) {
    float x = -5.2f + i * 3.45f;
    prop(r, p->cyl, white, x, 1.7f, 3.0f, .27f, 3.4f, .27f);
    prop(r, p->box, white, x, 3.42f, 3.0f, .75f, .22f, .75f);
    prop(r, p->box, white, x, .1f, 3.0f, .7f, .2f, .7f);
  }
  prop(r, p->box, white, 0, 3.65f, 3.0f, 11.4f, .22f, .34f);
  for (int i = 0; i <= 9; i++) prop(r, p->box, white, -5.4f + i * 1.2f, 3.8f, 1.5f, .16f, .16f, 3.4f);
  for (int i = 0; i <= 13; i++) prop(r, p->box, timber, 0, 3.92f, .05f + i * .25f, 11.4f, .09f, .09f);
  t3_release(white); t3_release(timber); t3_release(glass);
}
/* The long white one storey block west of the lab: a zigzag canopy, blue above, white under, a blue
 * and white triangle tiled dado below the windows, black mullions, four panel mural in the windows. w = 20 */
static void fk_butterfly(t3_object *r, const whimsy_parts *p) {
  t3_material *white = toy_paint(.93f, .93f, .9f), *blue = toy_paint(.27f, .55f, .77f), *aqua = toy_paint(.6f, .89f, .9f);
  t3_material *tile = toy_paint(.73f, .75f, .83f), *glass = toy_paint(.17f, .25f, .3f), *mur1 = toy_paint(.86f, .55f, .2f), *mur2 = toy_paint(.13f, .4f, .46f);
  prop(r, p->box, white, 0, 3.0f, -1.6f, 20.1f, 6.0f, 3.2f);
  prop(r, p->box, blue, 0, 3.2f, .04f, 20.1f, 1.0f, .06f);
  prop(r, p->box, tile, 0, .5f, .04f, 20.1f, 1.0f, .06f);
  prop(r, p->box, glass, 0, 1.9f, .05f, 19.6f, 1.8f, .05f);
  for (int i = 0; i <= 12; i++) prop(r, p->box, p->black, -9.6f + i * 1.6f, 1.9f, .08f, .06f, 1.9f, .04f);
  /* the door, and four mural panels */
  prop(r, p->box, p->black, -3.2f, 1.25f, .09f, 2.0f, 2.5f, .05f);
  prop(r, p->box, glass, -3.2f, 1.3f, .12f, 1.7f, 2.3f, .03f);
  for (int i = 0; i < 4; i++) { float x = -.4f + i * 1.65f; prop(r, p->box, i % 2 ? mur2 : mur1, x, 1.9f, .1f, 1.3f, 1.5f, .02f); t3_mesh *d = prop(r, p->box, i % 2 ? mur1 : mur2, x, 1.9f, .115f, .6f, .6f, .02f); t3_object_set_rotation(d, 0, 0, .785f); }
  /* the dado's triangles */
  mb tri[2] = {{0}, {0}};
  for (int i = 0; i < 26; i++) for (int row = 0; row < 2; row++) {
    float x = -9.9f + i * .78f, y0 = .1f + row * .42f, w = .78f, h = .36f;
    mb *m = &tri[(i + row) % 2];
    int k = mb_v(m, (t3_vec3){x, y0, .08f}, (t3_vec3){0, 0, 1}, 0, 0, 1);
    mb_v(m, (t3_vec3){x + w, y0, .08f}, (t3_vec3){0, 0, 1}, 0, 0, 1);
    mb_v(m, (t3_vec3){x + w * .5f, y0 + h, .08f}, (t3_vec3){0, 0, 1}, 0, 0, 1);
    mb_t(m, k, k + 1, k + 2);
  }
  t3_geometry *g0 = mb_done(&tri[0]), *g1 = mb_done(&tri[1]);
  add(r, g0, p->cream, false); add(r, g1, blue, false); t3_release(g0); t3_release(g1);
  /* the butterfly canopy: five folds. Top face aqua edged, underside white. */
  mb up = {0}, un = {0}, edge = {0};
  float x0 = -10.05f, fw = 20.1f / 5, y_lo = 3.65f, y_hi = 4.6f, z1 = 1.9f;
  for (int i = 0; i < 5; i++) {
    float a = x0 + i * fw, b = a + fw * .5f, c = a + fw;
    t3_vec3 A = {a, y_hi, z1}, B = {b, y_lo, z1}, C = {c, y_hi, z1}, A0 = {a, y_hi - .25f, 0}, B0 = {b, y_lo + .2f, 0}, C0 = {c, y_hi - .25f, 0};
    circus_quad(&up, A0, A, B, B0); circus_quad(&up, B0, B, C, C0);
    circus_quad(&un, B0, B, A, A0); circus_quad(&un, C0, C, B, B0);
    t3_vec3 A2 = {a, y_hi + .22f, z1 + .03f}, B2 = {b, y_lo + .22f, z1 + .03f}, C2 = {c, y_hi + .22f, z1 + .03f}, A3 = {a, y_hi, z1 + .03f}, B3 = {b, y_lo, z1 + .03f}, C3 = {c, y_hi, z1 + .03f};
    circus_quad(&edge, A3, B3, B2, A2); circus_quad(&edge, B3, C3, C2, B2);
  }
  t3_geometry *gu = mb_done(&up), *gd = mb_done(&un), *ge = mb_done(&edge);
  add(r, gu, blue, false); add(r, gd, white, false); add(r, ge, aqua, false);
  t3_release(gu); t3_release(gd); t3_release(ge);
  t3_release(white); t3_release(blue); t3_release(aqua); t3_release(tile); t3_release(glass); t3_release(mur1); t3_release(mur2);
}
/* Cream ribbed metal over shop windows papered from inside, a brick plinth. w = 21 */
static void fk_cream_block(t3_object *r, const whimsy_parts *p, t3_material *brick) {
  t3_material *cream = toy_paint(.95f, .92f, .74f), *glass = toy_paint(.3f, .32f, .3f), *conc = toy_paint(.65f, .64f, .6f);
  prop(r, p->box, cream, 0, 3.4f, -1.6f, 21, 6.8f, 3.2f);
  for (int i = 0; i < 52; i++) prop(r, p->box, cream, -10.2f + i * .4f, 5.0f, .06f, .12f, 3.6f, .1f);
  prop(r, p->box, conc, 0, 3.1f, .06f, 21, .4f, .16f);
  for (int i = 0; i < 4; i++) {
    float x = -7.8f + i * 5.2f;
    prop(r, p->box, glass, x, 1.75f, .03f, 4.8f, 2.3f, .06f);
    for (int k = -1; k <= 1; k++) prop(r, p->box, conc, x + k * 2.4f, 1.75f, .06f, .05f, 2.3f, .03f);
    mesa_masonry(r, brick, x, .3f, .05f, 4.9f, .6f, .1f);
  }
  for (int i = 0; i < 5; i++) prop(r, p->box, conc, -10.3f + i * 5.2f, 1.6f, .16f, .55f, 3.2f, .32f);
  t3_release(cream); t3_release(glass); t3_release(conc);
}
/* Pomeroy Building: two white storeys, a red tile arcade on round columns, green window awnings. w = 10 at the corner */
static void fk_red_tile_arcade(t3_object *r, const whimsy_parts *p, float w, float h, t3_material *wall) {
  t3_material *tile = toy_paint(.75f, .32f, .21f), *glass = toy_paint(.1f, .13f, .14f);
  prop(r, p->box, wall, 0, h * .5f, -1.8f, w, h, 3.6f);
  prop(r, p->box, glass, 0, 1.6f, -.02f, w - .6f, 2.8f, .05f);
  for (int i = 0; i <= (int)(w / 2.4f); i++) prop(r, p->box, p->black, -w * .5f + .3f + i * 2.4f, 1.6f, .02f, .06f, 2.8f, .04f);
  int nc = (int)(w / 3.2f); if (nc < 1) nc = 1;
  for (int i = 0; i <= nc; i++) {
    float x = -w * .5f + .3f + i * (w - .6f) / nc;
    prop(r, p->cyl, p->cream, x, 1.75f, 2.6f, .19f, 3.5f, .19f);
    prop(r, p->box, p->cream, x, .12f, 2.6f, .46f, .24f, .46f);
  }
  prop(r, p->box, p->cream, 0, 3.55f, 2.6f, w, .22f, .25f);
  t3_mesh *t = prop(r, p->box, tile, 0, 4.0f, 1.5f, w + .2f, .14f, 3.5f); t3_object_set_rotation(t, .3f, 0, 0);
  for (int i = 0; i < (int)(w / .32f); i++) { t3_mesh *q = prop(r, p->box, tile, -w * .5f + .16f + i * .32f, 3.5f, 3.2f, .08f, .07f, .3f); t3_object_set_rotation(q, .3f, 0, 0); }
  t3_release(tile); t3_release(glass);
}
static void fk_pomeroy(t3_object *r, const whimsy_parts *p) {
  t3_material *white = toy_paint(.92f, .9f, .84f), *green = toy_paint(.1f, .4f, .27f), *glass = toy_paint(.12f, .15f, .17f);
  fk_red_tile_arcade(r, p, 10, 10.4f, white);
  for (int i = -1; i <= 1; i += 2) {
    prop(r, p->box, glass, i * 2.6f, 6.6f, .03f, 1.2f, 1.9f, .05f);
    prop(r, p->box, p->cream, i * 2.6f, 7.65f, .03f, 1.6f, .14f, .12f);
    t3_mesh *a = prop(r, p->box, green, i * 2.6f, 5.55f, .5f, 1.8f, .06f, 1.1f); t3_object_set_rotation(a, .5f, 0, 0);
    prop(r, p->ball, glass, i * 2.6f, 9.0f, .03f, .42f, .5f, .03f);
  }
  t3_mesh *a = prop(r, p->box, green, 0, 6.2f, .7f, 2.8f, .07f, 1.4f); t3_object_set_rotation(a, .5f, 0, 0);
  prop(r, p->box, glass, 0, 7.0f, .03f, 2.2f, 1.1f, .05f);
  prop(r, p->box, p->cream, 0, 10.45f, -1.8f, 10.3f, .18f, 3.8f);
  mesa_name(r, "POMEROY BUILDING", 0, 8.1f, .04f, 5.2f, .34f);
  mesa_name(r, "POMEROY'S", 2.6f, 4.75f, .04f, 3.8f, .62f);
  t3_release(white); t3_release(green); t3_release(glass);
}
/* Old Brick House: white parapet canopy with a tall fin sign over green framed windows. w = 10 */
static void fk_old_brick(t3_object *r, const whimsy_parts *p) {
  t3_material *white = toy_paint(.93f, .93f, .9f), *glass = toy_paint(.1f, .13f, .15f), *blue = toy_paint(.25f, .55f, .9f);
  prop(r, p->box, white, 0, 2.4f, -1.6f, 10, 4.8f, 3.2f);
  prop(r, p->box, glass, 0, 1.6f, .02f, 9.4f, 2.8f, .05f);
  for (int i = 0; i <= 5; i++) prop(r, p->box, p->black, -4.7f + i * 1.88f, 1.6f, .04f, .07f, 2.8f, .04f);
  t3_mesh *c = prop(r, p->box, white, 0, 4.5f, .7f, 10.2f, .8f, 1.7f); t3_object_set_rotation(c, -.1f, 0, 0);
  mesa_name(r, "OLD BRICK HOUSE VINTAGE MARKET", -.1f, 3.95f, 1.58f, 6.2f, .24f);
  /* the tall white fin with its blue lettering, up over the roof */
  t3_mesh *f = prop(r, p->box, white, 3.3f, 8.8f, -1, 1.1f, 7.6f, .2f); t3_object_set_rotation(f, 0, .3f, -.1f);
  t3_mesh *g = prop(r, p->box, blue, 3.3f, 9.0f, -.86f, .6f, 5.4f, .05f); t3_object_set_rotation(g, 0, .3f, -.1f);
  t3_release(white); t3_release(glass); t3_release(blue);
}
/* One Macdonald: a brick ground floor with cream arched awnings, the four storey office behind it comes from the footprint. w = 40 */
static void fk_one_macdonald(t3_object *r, const whimsy_parts *p, t3_material *brick) {
  t3_material *cream = toy_paint(.93f, .89f, .78f), *glass = toy_paint(.1f, .14f, .17f), *wood = toy_paint(.7f, .5f, .28f);
  mesa_masonry(r, brick, 0, 2.6f, -1.2f, 40, 5.2f, 2.4f);
  for (int i = 0; i < 10; i++) {
    float x = -18 + i * 4;
    prop(r, p->box, i % 3 == 1 ? wood : glass, x, 1.55f, .03f, 2.9f, 3.1f, .06f);
    prop(r, p->box, p->black, x, 1.55f, .05f, .05f, 3.1f, .03f);
    t3_mesh *a = prop(r, p->box, cream, x, 3.55f, .5f, 3.1f, .06f, 1.15f); t3_object_set_rotation(a, .7f, 0, 0);
    prop(r, p->box, cream, x, 3.14f, .92f, 3.1f, .32f, .05f);
  }
  mesa_name(r, "ONE MACDONALD", -12, 4.55f, .03f, 6, .48f);
  t3_release(cream); t3_release(glass); t3_release(wood);
}

/* Generic storefronts, by style: 0 stucco with a parapet step, 1 white stepped, 2 brick parapet and green
 * awning, 3 white fin canopy, 4 red tile arcade, 5 dark timber canopy. Width w, sign label s. */
static void fk_generic(t3_object *r, const whimsy_parts *p, t3_material *brick, int style, float w, const char *label, uint32_t h) {
  float H = 5.2f + (h % 3) * .5f;
  static const float cols[3][3] = {{.86f, .8f, .68f}, {.92f, .91f, .87f}, {.74f, .66f, .56f}};
  t3_material *wall = toy_paint(cols[style == 1 ? 1 : h % 3][0], cols[style == 1 ? 1 : h % 3][1], cols[style == 1 ? 1 : h % 3][2]);
  t3_material *glass = toy_paint(.17f, .24f, .29f), *green = toy_paint(.12f, .4f, .28f);
  if (style == 4) {
    fk_red_tile_arcade(r, p, w, H, wall);
    if (label) mesa_name(r, label, 0, 4.85f, .04f, fminf(w * .75f, 5), .55f);
  } else {
    prop(r, p->box, wall, 0, H * .5f, -1.6f, w, H, 3.2f);
    if (style == 2) { mesa_masonry(r, brick, 0, H + .25f, -.1f, w, 1.9f, .3f); prop(r, p->box, p->cream, 0, H + 1.22f, -.1f, w + .1f, .08f, .36f); }
    if (style == 1) { prop(r, p->box, wall, 0, H + .4f, -.2f, w * .6f, .9f, .3f); prop(r, p->box, p->cream, 0, H + .87f, -.2f, w * .6f + .1f, .06f, .36f); }
    prop(r, p->box, p->cream, 0, H, .05f, w + .05f, .16f, .2f);
    prop(r, p->box, style == 5 ? p->brown : p->black, 0, 1.6f, .03f, w - .4f, 3.0f, .06f);
    prop(r, p->box, glass, 0, 1.75f, .07f, w - .8f, 2.4f, .03f);
    int nb = (int)(w / 2.2f); if (nb < 1) nb = 1;
    for (int i = 0; i <= nb; i++) prop(r, p->box, p->black, -w * .5f + .4f + i * (w - .8f) / nb, 1.75f, .09f, .05f, 2.4f, .03f);
    prop(r, p->box, p->lamp, 0, 2.96f, .1f, w - .9f, .03f, .03f);
    if (style == 2 || style == 0) { t3_mesh *a = prop(r, p->box, style == 2 ? green : p->red, 0, 3.4f, .6f, w - .5f, .06f, 1.3f); t3_object_set_rotation(a, .4f, 0, 0); }
    if (style == 3) { prop(r, p->box, wall, 0, 3.5f, .9f, w, .25f, 1.9f); }
    if (style == 5) { prop(r, p->box, p->brown, 0, 3.3f, .9f, w, .14f, 1.8f); for (int i = -1; i <= 1; i += 2) prop(r, p->box, p->brown, i * (w * .5f - .3f), 1.6f, 1.7f, .16f, 3.2f, .16f); prop(r, p->box, glass, 0, 4.3f, .03f, w * .8f, .9f, .05f); }
    if (label) mesa_name(r, label, 0, style == 3 ? 3.5f : 4.35f, style == 3 ? 1.88f : .1f, fminf(w * .8f, 5.5f), .5f);
  }
  t3_release(wall); t3_release(glass); t3_release(green);
}

/* ── the street ── */
typedef struct { t3_geometry *body, *cabin, *tire, *disc; t3_material *paint[6], *glass, *rubber, *alloy, *head, *tail; } car_parts;
static t3_geometry *car_shell(bool cabin) {
  static const float body[][4] = {{-2.3f, .45f, .75f, .75f}, {-2.23f, .40f, .83f, .82f}, {-2.05f, .35f, .91f, .88f}, {-1.25f, .35f, 1, .91f}, {1.15f, .35f, .96f, .91f}, {2.05f, .39f, .83f, .86f}, {2.23f, .43f, .76f, .79f}, {2.3f, .47f, .69f, .72f}};
  static const float roof[][4] = {{-1.35f, .94f, .98f, .8f}, {-.75f, .96f, 1.48f, .74f}, {.48f, .94f, 1.48f, .74f}, {1.18f, .92f, .96f, .8f}};
  static const float body_x[] = {-2.3f, -2.05f, -1.85f, -1.70f, -1.43f, -1.15f, -1.01f, -.6f, .6f, 1.01f, 1.15f, 1.43f, 1.70f, 1.85f, 2.05f, 2.3f};
  int count = cabin ? 4 : (int)(sizeof body_x / sizeof body_x[0]);
  t3_vec3 rings[16][8]; mb b = {0};
  for (int i = 0; i < count; i++) {
    float x, lo, hi, w, ch = cabin ? .025f : .065f;
    if (cabin) { x = roof[i][0]; lo = roof[i][1]; hi = roof[i][2]; w = roof[i][3]; }
    else {
      x = body_x[i]; int section = 0;
      while (section < 6 && x > body[section + 1][0]) section++;
      float u = (x - body[section][0]) / (body[section + 1][0] - body[section][0]);
      lo = body[section][1] + u * (body[section + 1][1] - body[section][1]);
      hi = body[section][2] + u * (body[section + 1][2] - body[section][2]);
      w = body[section][3] + u * (body[section + 1][3] - body[section][3]);
      float dx = fabsf(x) - 1.43f;
      if (fabsf(dx) < .42f) lo = fmaxf(lo, .35f + sqrtf(.42f * .42f - dx * dx));
    }
    t3_vec3 rr[] = {{x, lo, -w + ch}, {x, lo + ch, -w}, {x, hi - ch, -w}, {x, hi, -w + ch}, {x, hi, w - ch}, {x, hi - ch, w}, {x, lo + ch, w}, {x, lo, w - ch}};
    memcpy(rings[i], rr, sizeof rr);
  }
  for (int i = 1; i < count; i++) for (int j = 0; j < 8; j++) circus_quad(&b, rings[i - 1][j], rings[i - 1][(j + 1) % 8], rings[i][(j + 1) % 8], rings[i][j]);
  for (int j = 1; j < 7; j++) {
    circus_quad(&b, rings[0][0], rings[0][j], rings[0][j + 1], rings[0][j + 1]);
    circus_quad(&b, rings[count - 1][0], rings[count - 1][j + 1], rings[count - 1][j], rings[count - 1][j]);
  }
  return mb_done(&b);
}
static void fk_car(t3_object *r, const whimsy_parts *p, const car_parts *c, int colour) {
  t3_material *m = c->paint[colour % 6];
  prop(r, c->body, m, 0, .03f, 0, 1, 1, 1);
  prop(r, c->cabin, c->glass, 0, .03f, 0, 1, 1, 1);
  prop(r, p->box, m, -.15f, 1.5f, 0, 1.35f, .05f, 1.44f);
  for (int side = -1; side <= 1; side += 2) {
    for (int axle = -1; axle <= 1; axle += 2) {
      float wx = axle * 1.43f, wz = side * .9f;
      t3_mesh *t = prop(r, c->disc, c->rubber, wx, .35f, wz, .35f, .22f, .35f); t3_object_set_rotation(t, 1.5707963f, 0, 0);
      t3_mesh *h = prop(r, c->disc, c->alloy, wx, .35f, wz + side * .115f, .2f, .02f, .2f); t3_object_set_rotation(h, 1.5707963f, 0, 0);
    }
    prop(r, p->box, c->head, 2.28f, .72f, side * .56f, .06f, .12f, .33f);
    prop(r, p->box, c->tail, -2.28f, .75f, side * .58f, .06f, .14f, .3f);
  }
}
/* the catenary pole: teal, tapering, with a curved double lamp arm to each side, and two cross arms for the wires */
static void fk_pole(t3_object *r, const whimsy_parts *p, t3_material *teal, t3_material *wire) {
  prop(r, p->cyl, teal, 0, .4f, 0, .26f, .8f, .26f);
  prop(r, p->cyl, teal, 0, 4.5f, 0, .13f, 9, .13f);
  for (int side = -1; side <= 1; side += 2) {
    t3_mesh *a = prop(r, p->box, teal, 0, 7.6f, side * 1.6f, .07f, .07f, 3.4f); t3_object_set_rotation(a, side * -.35f, 0, 0);
    t3_mesh *b = prop(r, p->box, teal, 0, 8.6f, side * 1.5f, .05f, .05f, 3.1f); t3_object_set_rotation(b, side * -.15f, 0, 0);
    prop(r, p->cyl, p->lamp, 0, 8.05f, side * 3.2f, .13f, .22f, .13f);
    prop(r, p->box, teal, 0, 8.25f, side * 3.2f, .34f, .1f, .34f);
    prop(r, p->box, wire, 0, 7.95f, side * 2.2f, .05f, .3f, .05f);
  }
}
/* the black fluted lamp post with a shade on a curved arm */
static void fk_lamp(t3_object *r, const whimsy_parts *p, t3_material *ink) {
  prop(r, p->cyl, ink, 0, .32f, 0, .15f, .64f, .15f);
  prop(r, p->cyl, ink, 0, 2.3f, 0, .065f, 4.2f, .065f);
  prop(r, p->ball, ink, 0, .7f, 0, .1f, .05f, .1f);
  t3_mesh *a = prop(r, p->box, ink, .28f, 4.42f, 0, .6f, .05f, .05f); t3_object_set_rotation(a, 0, 0, .2f);
  prop(r, p->cone, ink, .55f, 4.5f, 0, .26f, .22f, .26f);
  prop(r, p->ball, p->lamp, .55f, 4.34f, 0, .13f, .1f, .13f);
}
static void fk_tree(t3_object *r, const whimsy_parts *p, t3_material *bark, t3_material *leaf, t3_material *leaf2, uint32_t h, float s) {
  prop(r, p->cyl, bark, 0, 1.2f * s, 0, .07f * s, 2.4f * s, .07f * s);
  for (int i = 0; i < 9; i++) {
    float a = i * 2.2f + h % 5, rr = (.3f + (i % 3) * .42f) * s;
    t3_mesh *m = prop(r, p->ball, i % 2 ? leaf : leaf2, cosf(a) * rr, (2.3f + (i % 4) * .5f) * s, sinf(a) * rr, (.55f - (i % 3) * .08f) * s, .42f * s, (.55f - (i % 3) * .08f) * s);
    t3_object_set_rotation(m, 0, a, .4f);
  }
  for (int i = 0; i < 3; i++) { float a = i * 2.1f + h % 3; t3_mesh *b = prop(r, p->box, bark, cosf(a) * .3f * s, 2.3f * s, sinf(a) * .3f * s, .05f * s, .9f * s, .05f * s); t3_object_set_rotation(b, sinf(a) * .5f, 0, -cosf(a) * .5f); }
}
static void fk_palm(t3_object *r, const whimsy_parts *p, t3_material *bark, t3_material *frond, float H, uint32_t h) {
  prop(r, p->cyl, bark, 0, H * .5f, 0, .2f, H, .2f);
  prop(r, p->ball, bark, 0, H, 0, .36f, .55f, .36f);
  mb f = {0};
  for (int i = 0; i < 10; i++) {
    float a = i * .628f + h % 7, c = cosf(a), s = sinf(a), L = 3.2f, droop = .95f + (i % 2) * .35f;
    for (int k = 0; k < 4; k++) {
      float t0 = k / 4.f, t1 = (k + 1) / 4.f, w0 = .34f * sinf(.15f + t0 * 3.0f), w1 = .34f * sinf(.15f + t1 * 3.0f);
      t3_vec3 c0 = {c * L * t0, H + .3f + .8f * t0 - droop * 2.2f * t0 * t0, s * L * t0}, c1 = {c * L * t1, H + .3f + .8f * t1 - droop * 2.2f * t1 * t1, s * L * t1};
      t3_vec3 A = {c0.x - s * w0, c0.y, c0.z + c * w0}, B = {c0.x + s * w0, c0.y, c0.z - c * w0}, C = {c1.x + s * w1, c1.y, c1.z - c * w1}, D = {c1.x - s * w1, c1.y, c1.z + c * w1};
      circus_quad(&f, A, B, C, D); circus_quad(&f, D, C, B, A);
    }
  }
  t3_geometry *g = mb_done(&f); add(r, g, frond, false); t3_release(g);
}
static void fk_planter(t3_object *r, const whimsy_parts *p, t3_material *conc, t3_material *dirt, t3_material *bark, t3_material *leaf, t3_material *leaf2, uint32_t h) {
  prop(r, p->box, conc, 0, .1f, 0, 3.2f, .22f, 1.6f);
  prop(r, p->box, dirt, 0, .2f, 0, 2.8f, .06f, 1.25f);
  fk_tree(r, p, bark, leaf, leaf2, h, .85f);
}
static void fk_bench(t3_object *r, const whimsy_parts *p, t3_material *green) {
  for (int i = 0; i < 4; i++) prop(r, p->box, green, 0, .47f, -.2f + i * .13f, 1.8f, .03f, .09f);
  for (int i = 0; i < 3; i++) prop(r, p->box, green, 0, .62f + i * .13f, -.26f, 1.8f, .08f, .03f);
  for (int s = -1; s <= 1; s += 2) { prop(r, p->box, green, s * .8f, .23f, 0, .05f, .46f, .5f); prop(r, p->box, green, s * .8f, .6f, -.26f, .05f, .5f, .03f); }
}
static void fk_signal(t3_object *r, const whimsy_parts *p, t3_material *teal) {
  prop(r, p->cyl, teal, 0, 3.2f, 0, .1f, 6.4f, .1f);
  prop(r, p->box, teal, 0, 6.2f, 3.2f, .1f, .1f, 6.4f);
  for (int i = 0; i < 2; i++) { float z = 3.2f + i * 2.8f; prop(r, p->box, p->black, 0, 5.75f, z, .28f, .85f, .28f); prop(r, p->ball, p->green, -.15f, 5.5f, z, .08f, .08f, .08f); prop(r, p->ball, p->gold, -.15f, 5.75f, z, .08f, .08f, .08f); prop(r, p->ball, p->red, -.15f, 6.0f, z, .08f, .08f, .08f); }
}
/* a lattice cone of thin steel: the sculpture towers at Macdonald and by the station */
static void fk_tower(t3_object *r, const whimsy_parts *p, t3_material *steel, float H, float R) {
  mb m = {0};
  for (int i = 0; i < 6; i++) {
    float a = i * 1.0472f, b = a + 1.0472f;
    for (int k = 0; k < 8; k++) {
      float t0 = k / 8.f, t1 = (k + 1) / 8.f, r0 = R * (1 - t0 * .92f), r1 = R * (1 - t1 * .92f);
      t3_vec3 A = {cosf(a) * r0, H * t0, sinf(a) * r0}, B = {cosf(b) * r0, H * t0, sinf(b) * r0}, C = {cosf(b) * r1, H * t1, sinf(b) * r1}, D = {cosf(a) * r1, H * t1, sinf(a) * r1};
      /* a thin diagonal pair and a ring per panel */
      const float w = .05f;
      t3_vec3 pairs[3][2] = {{A, C}, {B, D}, {A, B}};
      for (int q = 0; q < 3; q++) {
        t3_vec3 u = pairs[q][0], v = pairs[q][1], d = {v.x - u.x, v.y - u.y, v.z - u.z};
        t3_vec3 nh = {-d.z, 0, d.x}; float nl = sqrtf(nh.x * nh.x + nh.z * nh.z); if (nl < 1e-4f) nh = (t3_vec3){1, 0, 0}, nl = 1;
        nh.x *= w / nl; nh.z *= w / nl;
        t3_vec3 a0 = {u.x - nh.x, u.y, u.z - nh.z}, b0 = {u.x + nh.x, u.y, u.z + nh.z}, c0 = {v.x + nh.x, v.y, v.z + nh.z}, d0 = {v.x - nh.x, v.y, v.z - nh.z};
        if (q == 2) { a0.y -= w; b0.y += w; c0.y += w; d0.y -= w; a0.x = u.x; a0.z = u.z; b0.x = u.x; b0.z = u.z; c0.x = v.x; c0.z = v.z; d0.x = v.x; d0.z = v.z; }
        circus_quad(&m, a0, b0, c0, d0); circus_quad(&m, d0, c0, b0, a0);
      }
    }
  }
  t3_geometry *g = mb_done(&m); add(r, g, steel, false); t3_release(g);
  prop(r, p->cyl, steel, 0, .25f, 0, R * 1.05f, .5f, R * 1.05f);
}
static void fk_shelter(t3_object *r, const whimsy_parts *p, t3_material *teal, t3_material *ink) {
  /* the bus shelter: pale green curved roof on two legs, a bench under it */
  for (int s = -1; s <= 1; s += 2) { prop(r, p->box, teal, s * 1.9f, 1.2f, -.3f, .1f, 2.4f, .1f); prop(r, p->box, teal, s * 1.9f, 2.4f, 0, .12f, .12f, 1.5f); }
  t3_mesh *a = prop(r, p->box, teal, 0, 2.5f, 0, 4.4f, .06f, 1.7f); t3_object_set_rotation(a, -.12f, 0, 0);
  for (int i = 0; i < 4; i++) prop(r, p->box, ink, 0, .47f, -.45f + i * .13f, 3, .03f, .09f);
  for (int s = -1; s <= 1; s += 2) prop(r, p->box, ink, s * 1.3f, .23f, -.25f, .05f, .46f, .5f);
}
static void fk_trellis_shelter(t3_object *r, const whimsy_parts *p, t3_material *timber, t3_material *leaf, t3_material *teal) {
  /* a station shelter: timber posts and a slatted roof with climbing plants */
  for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2) {
    prop(r, p->box, timber, sx * 4.2f, 1.85f, sz * 1.5f, .24f, 3.7f, .24f);
    for (int k = 0; k < 3; k++) prop(r, p->ball, leaf, sx * 4.2f + (k % 2 ? .1f : -.1f), .5f + k * 1.2f, sz * 1.5f, .22f, .55f, .22f);
  }
  for (int sz = -1; sz <= 1; sz += 2) prop(r, p->box, timber, 0, 3.75f, sz * 1.5f, 9, .2f, .14f);
  for (int i = 0; i < 22; i++) prop(r, p->box, timber, -4.3f + i * .41f, 3.9f, 0, .1f, .1f, 3.6f);
  for (int i = 0; i < 4; i++) prop(r, p->ball, leaf, -3 + i * 2, 3.98f, (i % 2 ? .7f : -.6f), .7f, .14f, .5f);
  for (int s = -1; s <= 1; s += 2) { for (int i = 0; i < 3; i++) prop(r, p->box, teal, s * 2.2f, .5f, -.2f + i * .14f, 2.4f, .04f, .1f); prop(r, p->box, teal, s * 1.2f, .25f, 0, .05f, .5f, .5f); prop(r, p->box, teal, s * 3.2f, .25f, 0, .05f, .5f, .5f); }
}
static void fk_statue(t3_object *r, const whimsy_parts *p, t3_material *bronze, t3_material *conc) {
  prop(r, p->box, conc, 0, .25f, 0, 1.2f, .5f, 1.2f);
  prop(r, p->cyl, bronze, 0, 1.0f, 0, .18f, 1.0f, .18f);
  prop(r, p->ball, bronze, 0, 1.68f, 0, .13f, .16f, .13f);
  prop(r, p->box, bronze, .22f, 1.2f, 0, .1f, .6f, .1f); prop(r, p->box, bronze, -.22f, 1.2f, 0, .1f, .6f, .1f);
}

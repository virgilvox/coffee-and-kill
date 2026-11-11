/* stubs.c: weak stand-ins for the modules still being ported, so the cart links
 * while they land; each real definition overrides its stub. */
#include "audio.h"
#include "ui.h"
#include "arena.h"
#include <stddef.h>
#define W __attribute__((weak))
W void audio_init(uint32_t rate) { (void)rate; }
W void audio_render(float *ring, uint32_t cap, uint32_t *cursor, uint32_t rate, double ms) {
  (void)ring; (void)cap; (void)cursor; (void)rate; (void)ms; }
W double audio_time(void) { return 0; }
W double audio_latency(void) { return 0; }
W void sfx_whoosh(float s) { (void)s; }
W void sfx_hit(bool h) { (void)h; }
W void sfx_clang(void) {}
W void sfx_block(void) {}
W void sfx_kick(void) {}
W void sfx_shock(void) {}
W void sfx_ko(void) {}
W void sfx_bell(void) {}
W void voice_say(const char *n) { (void)n; }
W void sfx_ui(void) {}
W void sfx_ui_ok(void) {}
W void sfx_step(void) {}
W void sfx_dodge(void) {}
W void sfx_special(void) {}
W void sfx_fight(void) {}
W void sfx_win(void) {}
W void sfx_duck(float s, float a) { (void)s; (void)a; }
W bool sfx_toggle_mute(void) { return false; }
W bool sfx_is_muted(void) { return false; }
W void sfx_by_name(const char *n, float a) { (void)n; (void)a; }
W void (*sfx_tap)(const char *name, float arg);
W void music_set_level(float s) { (void)s; }
W bool music_toggle(void) { return true; }
W double music_dance_start(int song) { (void)song; return -1; }
W void music_dance_stop(void) {}

/* ---- ui.h, arena.h, geom.h, dance.h, songs.h, online.h until they land ---- */
#include "geom.h"
#include "dance.h"
#include "songs.h"
#include "online.h"
W void (*ui_on_olweapon)(const char *w);
W void (*ui_on_challenge)(const char *peer_id);
W void (*ui_on_room_enter)(void);
static const char *stub_screen;
W void ui_init(void) {}
W void ui_frame(float dt) { (void)dt; }
W void ui_draw(void) {}
W const char *ui_screen(void) { return stub_screen; }
W void ui_show(const char *s) { stub_screen = s; }
W void ui_hide_all(void) { stub_screen = NULL; }
W bool ui_key(int c) { (void)c; return false; }
W bool ui_key_used(int i) { (void)i; return false; }
W void ui_pointer(float x, float y, bool p) { (void)x; (void)y; (void)p; }
W void ui_set_select(ck_sel *s, int m) { (void)s; (void)m; }
W void ui_set_names(const char *a, const char *b, const char *c, const char *d) { (void)a; (void)b; (void)c; (void)d; }
W void ui_set_health(int i, float p, bool n) { (void)i; (void)p; (void)n; }
W void ui_set_meter(int i, float p) { (void)i; (void)p; }
W void ui_set_pips(int i, int o, int f) { (void)i; (void)o; (void)f; }
W void ui_set_timer(float t) { (void)t; }
W void ui_set_round(const char *s) { (void)s; }
W void ui_set_hint(const char *s) { (void)s; }
W void ui_combo(int i, int n, const char *s) { (void)i; (void)n; (void)s; }
W void ui_announce(const char *a, const char *b, bool s) { (void)a; (void)b; (void)s; }
W void ui_clear_announce(void) {}
W void ui_toast(const char *s) { (void)s; }
W void ui_fx(const char *t, float x, float y, const char *c) { (void)t; (void)x; (void)y; (void)c; }
W void ui_flash(uint32_t c, float a, float m) { (void)c; (void)a; (void)m; }
W void ui_hud(bool on) { (void)on; }
W void ui_set_end(const char *n, bool w, const char *s) { (void)n; (void)w; (void)s; }
W void ui_load(float p, const char *m) { (void)p; (void)m; }
W void ui_load_error(const char *m) { (void)m; }

W void arena_scene_setup(t3_renderer *r, t3_scene *s) { (void)r; s->has_background = true; s->background = t3_color_hex(723983); }
W int arena_build(t3_scene *s, int st) {
  (void)st;
  t3_object_add((t3_object *)s, t3_hemisphere_light_new(13621503, 3814184, 0.85f));
  t3_light *sun = t3_directional_light_new(16773340, 2.4f);
  t3_object_set_position(sun, -7, 12, 5); t3_object_add((t3_object *)s, sun);
  return 1;
}
W void arena_update(float dt, float t) { (void)dt; (void)t; }
W void arena_set_focus(float x, float y, float z) { (void)x; (void)y; (void)z; }
W void arena_fx_draw(void) {}

W t3_geometry *geom_tube(const t3_vec3 *pts, int n, bool cc, int cen, float ten, int ts, float r, int rs, bool closed) {
  (void)pts; (void)n; (void)cc; (void)cen; (void)ten; (void)ts; (void)rs; (void)closed;
  return t3_cylinder_geometry_new(r, r, 0.6f, 8, 1, false); }
W t3_geometry *geom_extrude(const float *xy, int n, float d, bool b, float bt, float bs, int seg) {
  (void)xy; (void)n; (void)b; (void)bt; (void)bs; (void)seg;
  return t3_box_geometry_new(0.1f, 0.47f, d, 1, 1, 1); }

W void (*dance_on_move)(int player, int lane, int judgement);
W void dance_init(void) {}
W void dance_start(uint32_t seed, const dance_player *p, int n, void (*ns)(mp_val *)) { (void)seed; (void)p; (void)n; (void)ns; }
W void dance_update(float dt) { (void)dt; }
W void dance_draw(void) {}
W bool dance_active(void) { return false; }
W void dance_cancel(void) {}
W float dance_beat_pulse(void) { return 0; }
W bool dance_take_result(dance_result *o) { (void)o; return false; }
W void dance_remote_hit(int i, int j, int s) { (void)i; (void)j; (void)s; }
W void dance_remote_final(int s, int m) { (void)s; (void)m; }

W void song_pick(int last, uint32_t *seed, int *idx) { (void)last; *seed = 0; *idx = 0; }

W void online_init(void) {}
W void online_update(float dt) { (void)dt; }
W void online_open(void) {}
W void online_back(void) {}
W void online_join(void) {}
W void online_main_lobby(void) {}
W void online_new_code(void) {}
W void online_copy_link(void) {}
W void online_copy_pad(void) {}
W void online_accept(void) {}
W void online_decline(void) {}
W void online_open_pads(void) {}
W void online_leave_room_status(const char *m) { (void)m; }
W bool online_in_match(void) { return false; }
W int online_role(void) { return 0; }
W void online_snap(mp_val *v) { mp_free(v); }
W void online_intent(mp_val *v) { mp_free(v); }
W void online_ctl(mp_val *v) { mp_free(v); }
W double online_idle_ms(void) { return 0; }
W void online_end_match(void) {}
static song_t stub_song = {"funk", "BOILER ROOM FUNK", 112, 60.f / 112 / 4, 10, 160, 6, 24, NULL, 0, NULL};
W int song_count(void) { return 1; }
W const song_t *song_get(int i) { (void)i; return &stub_song; }
W const song_t *song_from_seed(uint32_t s) { (void)s; return &stub_song; }
W int song_chart(const song_t *s, chart_note *o, int max) {
  (void)s; int n = 0; for (int b = 8; b < 32 && n < max; b += 1) { o[n].beat = (float)b; o[n].lane = b % 4; n++; } return n; }

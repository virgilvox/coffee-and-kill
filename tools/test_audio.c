/* test_audio.c: an audio-only game for the cart (OUT=build-audio, see
 * tools/audio_pack.sh): the fight music at each level, every SFX, two
 * dance songs, mute and the music toggle, on a fixed schedule. */
#include "game.h"
#include "ck.h"
#include "audio.h"
#include "songs.h"
#include <stdio.h>
static int f;
static double dance_t0 = -1; static int dance_end_frame = -1;
void game_init(void) {}
bool game_boot(int stage) { (void)stage; return true; }
static void at(double s, void (*fn)(void)) { if (f == (int)(s * 60)) fn(); }
static void l035(void) { music_set_level(0.35f); ck_log("t=%.2f level .35", audio_time()); }
static void l1(void) { music_set_level(1); ck_log("t=%.2f level 1", audio_time()); }
static void l015(void) { music_set_level(0.15f); ck_log("t=%.2f level .15", audio_time()); }
static void d0(void) { dance_t0 = music_dance_start(0); music_set_level(1); dance_end_frame = f + (int)((song_get(0)->total_steps * song_get(0)->sec_per_step + 2) * 60); ck_log("t=%.2f dance funk t0 %.3f", audio_time(), dance_t0); }
static void d3(void) { dance_t0 = music_dance_start(3); dance_end_frame = f + (int)((song_get(3)->total_steps * song_get(3)->sec_per_step + 2) * 60); ck_log("t=%.2f dance surf t0 %.3f", audio_time(), dance_t0); }
static void mute(void) { ck_log("t=%.2f muted %d", audio_time(), sfx_toggle_mute()); }
static void ntog(void) { ck_log("t=%.2f music %d", audio_time(), music_toggle()); }
void game_frame(float dt) {
  (void)dt;
  at(1, l035); at(9, l1); at(16, l035);
  if (f >= 20 * 60 && f < 30 * 60 && f % 30 == 0) {
    int k = (f - 20 * 60) / 30;
    switch (k) {
      case 0: sfx_whoosh(1); break; case 1: sfx_whoosh(1.5f); break; case 2: sfx_hit(false); break; case 3: sfx_hit(true); break;
      case 4: sfx_clang(); break; case 5: sfx_block(); break; case 6: sfx_kick(); break; case 7: sfx_shock(); break;
      case 8: sfx_ko(); break; case 9: sfx_bell(); break; case 10: sfx_ui(); break; case 11: sfx_ui_ok(); break;
      case 12: sfx_step(); break; case 13: sfx_dodge(); break; case 14: sfx_special(); break; case 15: sfx_fight(); break;
      case 16: sfx_win(); break; default: break;
    }
  }
  at(31, d0);
  if (f == dance_end_frame && dance_t0 >= 0) { music_dance_stop(); ck_log("t=%.2f dance stop", audio_time()); dance_end_frame = -1; }
  at(62, l015); at(66, d3);
  at(99, mute); at(101, mute); at(103, ntog); at(105, ntog);
  f++;
}
void game_draw(void) { render_clear(0x101010); }

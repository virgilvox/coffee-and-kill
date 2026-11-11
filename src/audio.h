/* audio.h: the page's sound, both halves.
 *
 * SFX (sfx.c): the SA object, WebAudio voices built per call (oscillators
 * with exponential ramps, filtered noise, the sidechain duck) into the bus
 * chain of ie(): sfx gain 1 -> master 0.8 -> DynamicsCompressor(-14 dB, 5:1)
 * -> out, with the music bus (gain 0.36, ducked) into the same master.
 *
 * Music (music.cpp): the Bellows band of the page (tt): the fight score of
 * Dl()/wl()/gl()/il() on a 16th note clock at 140 bpm with swing, three
 * intensity levels set by music_set_level, and the dance break songs of
 * songs.c played by rl(). Bellows' C++ engine is the JS engine's port. */
#ifndef CK_AUDIO_H
#define CK_AUDIO_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void audio_init(uint32_t rate);
/* fill the wasmcart ring (interleaved stereo float) for ms of host time */
void audio_render(float *ring, uint32_t cap, uint32_t *cursor, uint32_t rate, double ms);
double audio_time(void);       /* AudioContext.currentTime: seconds of audio rendered */
double audio_latency(void);    /* outputLatency || baseLatency (the ring's depth) */

/* SA.* */
void sfx_whoosh(float s);
void sfx_hit(bool heavy);
void sfx_clang(void);
void sfx_block(void);
void sfx_kick(void);
void sfx_shock(void);
void sfx_ko(void);
void sfx_bell(void);
void sfx_ui(void);
void sfx_ui_ok(void);
void sfx_step(void);
void sfx_dodge(void);
void sfx_special(void);
void sfx_fight(void);
void sfx_win(void);
void sfx_duck(float s, float a);
bool sfx_toggle_mute(void);    /* returns muted */
bool sfx_is_muted(void);
/* SA._tap: called with the name and arguments of every whoosh, hit, clang,
 * block, kick, shock, ko, bell, dodge, special, fight and win (online host) */
extern void (*sfx_tap)(const char *name, float arg);
/* play one by name (online guest replaying the host's) */
void sfx_by_name(const char *name, float arg);

/* tt.* */
void music_set_level(float s);              /* SA.setMusic */
bool music_toggle(void);                    /* N: returns on */
/* tt.dance(true, track): start a dance song; returns the audio time the song's
 * first step plays at (the Promise's value), or -1 if there is no engine */
double music_dance_start(int song);
void music_dance_stop(void);                /* tt.dance(false) */

#ifdef __cplusplus
}
#endif
#endif

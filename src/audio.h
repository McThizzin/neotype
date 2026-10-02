/* audio.h - procedural sound effects.
 *
 * Knows nothing about the game: callers say "play this sound, this pitch,
 * this loud". Mapping game events to sounds lives in main.c.
 *
 * Depends on: miniaudio (vendored, device only). The mixer itself is
 * device-free so tests can render audio to a buffer.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>

typedef enum {
    SFX_HIT,
    SFX_MISS,
    SFX_KILL,
    SFX_LEVELUP,
    SFX_GAMEOVER,
    SFX_COUNT
} Sfx;

/* Opens the default output device. Returns false if there isn't one
 * (SSH session, container, no sound card); the game should carry on silently. */
bool audio_init(void);
void audio_shutdown(void);

/* Fire and forget. pitch: 1.0 = as synthesised, 2.0 = octave up.
 * gain: 0..1. Safe to call from the game thread; never blocks. */
void audio_play(Sfx id, float pitch, float gain);

void audio_set_muted(bool muted);
bool audio_is_muted(void);

/* Device-free core, used by tests and by audio_init(). */
void audio_mixer_init(void);                    /* synthesise the sample tables */
void audio_mix(float *out, unsigned frames);    /* mono float, 44100 Hz */

#define AUDIO_RATE 44100

#endif

/* sound.h - binds game events to sound effects.
 *
 * The seam between the two: audio knows nothing about the game, so this is
 * the one place that reads simulation state and asks for a sound. It owns no
 * timing and never touches the terminal, so it can be driven by a test.
 *
 * Depends on game (reads the counters) and audio (asks for effects).
 */
#pragma once

#include <stdbool.h>

#include "game.h"

/* Opens the audio device and builds the effect tables. False if there is no
 * output device (SSH, container, no sound card), in which case the game runs
 * on silently. Call before the TUI takes over the terminal. */
bool sound_open(void);
void sound_close(void);

/* Master switch. sound_open() sets this from the device result, so a machine
 * with no sound card stays quiet; while off, sound_sync() does nothing. This
 * is also where a mute key would flip it. */
void sound_set_enabled(bool on);
bool sound_enabled(void);

/* Call once per frame, after the sim has stepped. Turns the counter deltas
 * into effects. Counters reset when a run starts or is retried, so a state
 * change resyncs silently -- except the move into S_OVER, which is the
 * crash itself. */
void sound_sync(const Game *g);
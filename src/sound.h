/* sound.h - binds game events to sound effects.
 *
 * The seam between the two: audio knows nothing about the game, so this is
 * the one place that reads simulation state and asks for a sound. It owns no
 * timing and never touches the terminal, so it can be driven by a test.
 *
 * Whether sound is wanted is a menu setting held by the game (Game.sound),
 * which this mirrors onto the mixer; this module only tracks whether an
 * output device actually opened.
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

/* Call once per frame, after the sim has stepped. Turns the counter deltas
 * into effects. Counters reset when a run starts or is retried, so a state
 * change resyncs silently -- except the move into S_OVER, which is the
 * crash itself. */
void sound_sync(const Game *g);
/*
 * neotype - matrix-rain typing shooter for the terminal
 *
 * Build:   make
 * Run:     ./neotype
 * Test:    make test
 *
 * Letters rain down. Type a letter (case-sensitive!) to fire at the lowest
 * matching letter. Tougher letters take 2-3 hits and change colour as they
 * lose health. If any letter touches the red line above your prompt, it's
 * game over.
 *
 *   green = 1 hit   amber = 2 hits   red = 3 hits
 *
 * Keys: Tab pause | Enter clear prompt | Esc / Ctrl-C quit
 *
 * Needs a truecolor terminal (Ghostty, kitty, WezTerm, iTerm2, ...).
 *
 * Modules, bottom-up:
 *   util    rng, math, clock, utf-8          (no dependencies)
 *   term    output buffer, raw mode, signals  (POSIX)
 *   canvas  cell grid + diff renderer        (term, util)
 *   palette colour language                  (no dependencies)
 *   audio   procedural sfx and the mixer      (no dependencies, device only)
 *   game    simulation and rules             (util, palette)
 *   draw    paints game state to the canvas  (canvas, game, palette)
 *   input   stdin -> key events              (term, game)
 *   main    wiring and the fixed-step loop   (everything)
 */
#define _POSIX_C_SOURCE 200809L

#include "audio.h"
#include "canvas.h"
#include "draw.h"
#include "game.h"
#include "input.h"
#include "term.h"
#include "util.h"

#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---------- headless test harness ----------
 * Env-driven so the regression test needs no tty and no wall clock:
 *   NEOTYPE_SEED     xorshift seed (default: time/pid mix, as in normal play)
 *   NEOTYPE_HEADLESS =1  simulate instead of starting the TUI
 *   NEOTYPE_W / NEOTYPE_H  viewport size (default 100x30)
 *   NEOTYPE_FRAMES   frames to simulate (default 3600)
 * Emits a digest line every 60 frames; tests/golden.txt is the expected
 * output. See tests/run.sh.
 */
static int env_int(const char *k, int dflt) {
    const char *v = getenv(k);
    return v && *v ? atoi(v) : dflt;
}

static uint64_t env_seed(void) {
    const char *v = getenv("NEOTYPE_SEED");
    if (v && *v) return strtoull(v, NULL, 0);
    return (uint64_t)time(NULL) * 2654435761ULL ^ ((uint64_t)getpid() << 32);
}

/* Stimulus uses its own LCG so that drift in the game's RNG shows up in the
 * digest directly, rather than being amplified through the input script. */
static uint64_t ks = 0x1234567890abcdefULL;
static char next_key(void) {
    static const char alpha[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    ks ^= ks << 13; ks ^= ks >> 7; ks ^= ks << 17;
    return alpha[(ks >> 11) % (sizeof alpha - 1)];
}

static void headless_run(void) {
    int frames = env_int("NEOTYPE_FRAMES", 3600);
    int w = env_int("NEOTYPE_W", 100), h = env_int("NEOTYPE_H", 30);
    int retried = 0;
    if (w < MIN_W) w = MIN_W;
    if (h < MIN_H) h = MIN_H;

    rs = env_seed();
    for (int i = 0; i < 8; i++) rnd32();
    canvas_resize(w, h);
    game_set_viewport(w, h);
    game_set_state(S_TITLE);

    for (int f = 0; f < frames; f++) {
        /* Fixed stimulus: start, a key every other frame, and a few one-off
         * control keys so the pause / clear / backspace paths get covered. */
        if      (f == 1)   game_on_key('\r');
        else if (f == 300) game_on_key('\t');
        else if (f == 360) game_on_key('\t');
        else if (f == 500) game_on_key('\r');
        else if (f == 520) game_on_key(127);

        if (f > 1 && !(f & 1)) game_on_key(next_key());

        /* Once the crash overlay has settled, retry once, so game_reset() and
         * the S_OVER -> S_PLAY transition are covered as well. */
        if (!retried && game_over_t_reached(1.0f)) { game_on_key('\r'); retried = 1; }

        const Game *g = game();
        if (g->state == S_PLAY) game_update((float)FRAME);
        else if (g->state == S_OVER) game_advance_over((float)FRAME);
        draw_frame();

        if (f % 60 == 0 || f == frames - 1)
            printf("f=%04d state=%d score=%d kills=%d level=%d shots=%d hits=%d "
                   "combo=%d maxcombo=%d tlen=%d elapsed=%.3f "
                   "drops=%d beams=%d fx=%d rng=%016llx grid=%016llx\n",
                   f, g->state, g->score, g->kills, g->level, g->shots, g->hits,
                   g->combo, g->maxcombo, g->tlen, g->elapsed,
                   game_drops_alive(), game_beams_live(), game_fx_live(),
                   (unsigned long long)rs, (unsigned long long)canvas_hash());
    }
}

/* ---------- sound ----------
 * audio knows nothing about the game, so main reads the sim's counters and
 * turns the per-frame deltas into sounds. That keeps the dependency one-way
 * and means the rules never learn sound exists.
 *
 * A kill is also a hit (game_shoot bumps both counters), so hits are split
 * into kills and the remainder. Starting a run or retrying after a crash
 * zeroes the counters, so any state change resyncs silently -- except the
 * move into S_OVER, which is the crash itself.
 */
static struct { int shots, hits, kills, level, state; } snd_last;
static bool snd_ready;                 /* is snd_last a real state yet? */
static bool sound_on;                  /* no output device means play nothing */

static void snd_resync(const Game *g) {
    snd_last.shots = g->shots; snd_last.hits = g->hits;
    snd_last.kills = g->kills; snd_last.level = g->level;
    snd_last.state = g->state;
}

static void audio_sync(const Game *g) {
    if (!sound_on) return;

    /* A zeroed snd_last is not a game state -- level starts at 1, not 0 -- so
     * adopt the opening state silently instead of reading the gap as a jump. */
    if (!snd_ready) { snd_resync(g); snd_ready = true; return; }

    if (g->state != snd_last.state) {
        snd_resync(g);
        if (g->state == S_OVER) audio_play(SFX_GAMEOVER, 1.0f, 0.6f);
        return;
    }

    int kills = g->kills - snd_last.kills;
    int hits  = g->hits  - snd_last.hits  - kills;
    int miss  = g->shots - snd_last.shots - hits - kills;
    snd_last.shots = g->shots; snd_last.hits = g->hits; snd_last.kills = g->kills;

    /* One keypress arrives per frame in practice, but a paste can deliver
     * several at once; cap so a burst cannot flood the request ring. */
    for (int i = 0; i < kills && i < 4; i++) audio_play(SFX_KILL, 1.0f, 0.8f);
    for (int i = 0; i < hits  && i < 4; i++) audio_play(SFX_HIT,  1.0f, 0.6f);
    for (int i = 0; i < miss  && i < 4; i++) audio_play(SFX_MISS, 1.0f, 0.5f);

    if (g->level > snd_last.level) audio_play(SFX_LEVELUP, 1.0f, 0.7f);
    snd_last.level = g->level;
}

/* ---------- main ---------- */
int main(void) {
    if (getenv("NEOTYPE_HEADLESS")) { headless_run(); return 0; }
    if (!isatty(0) || !isatty(1)) { fputs("neotype needs an interactive terminal\n", stderr); return 1; }

    rs = env_seed();
    for (int i = 0; i < 8; i++) rnd32();

    /* Before term_init(): the audio stack probes the sound card and talks to
     * stderr, which is still an ordinary terminal at this point. */
    sound_on = audio_init();
    if (!sound_on) fputs("neotype: no audio output device, playing silently\n", stderr);

    term_init();

    int w, h;
    term_size(&w, &h);
    canvas_resize(w, h);
    game_set_viewport(w, h);

    double last = now(), next = last;
    while (!quit_flag && !game_quit_requested()) {
        double t0 = now(), wait = next - t0;
        if (wait > 0) {
            struct pollfd p = { 0, POLLIN, 0 };
            int pr = poll(&p, 1, (int)(wait * 1000.0) + 1);
            if (pr > 0 && (p.revents & (POLLIN | POLLHUP))) input_poll();
        }
        t0 = now();
        if (t0 < next) continue;

        double dt = t0 - last;
        last = t0;
        if (dt > 0.1) dt = 0.1;
        next = t0 + FRAME;

        term_size(&w, &h);
        if (w != canvas_w() || h != canvas_h()) {
            canvas_resize(w, h);
            game_set_viewport(w, h);
        }

        if (!canvas_too_small()) {
            const Game *g = game();
            if (g->state == S_PLAY) game_update((float)dt);
            else if (g->state == S_OVER) game_advance_over((float)dt);
            audio_sync(g);
        }
        draw_frame();
        present();
    }
    audio_shutdown();
    return 0;
}

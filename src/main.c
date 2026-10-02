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
 * Main menu and game over take 1 start/retry, 2 sound / main menu,
 * 3 capital letters. In play: letters fire | Tab pause | Enter clear |
 * Backspace | Esc / Ctrl-C quit.
 *
 * Needs a truecolor terminal (Ghostty, kitty, WezTerm, iTerm2, ...).
 *
 * Modules, bottom-up:
 *   util    rng, math, clock, utf-8          (no dependencies)
 *   term    output buffer, raw mode, signals  (POSIX)
 *   canvas  cell grid + diff renderer        (term, util)
 *   palette colour language                  (no dependencies)
 *   audio   procedural sfx and the mixer      (no dependencies, device only)
 *   sound   binds game events to effects      (game, audio)
 *   game    simulation and rules             (util, palette)
 *   draw    paints game state to the canvas  (canvas, game, palette)
 *   input   stdin -> key events              (term, game)
 *   main    wiring and the fixed-step loop   (everything)
 */
#define _POSIX_C_SOURCE 200809L

#include "canvas.h"
#include "draw.h"
#include "game.h"
#include "input.h"
#include "sound.h"
#include "term.h"
#include "util.h"

#include <poll.h>
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

static void digest(const Game *g, int f) {
    printf("f=%04d state=%d score=%d kills=%d level=%d shots=%d hits=%d "
           "combo=%d maxcombo=%d tlen=%d elapsed=%.3f "
           "drops=%d beams=%d fx=%d rng=%016llx grid=%016llx\n",
           f, g->state, g->score, g->kills, g->level, g->shots, g->hits,
           g->combo, g->maxcombo, g->tlen, g->elapsed,
           game_drops_alive(), game_beams_live(), game_fx_live(),
           (unsigned long long)rs, (unsigned long long)canvas_hash());
}

static void headless_run(void) {
    int frames = env_int("NEOTYPE_FRAMES", 3600);
    int w = env_int("NEOTYPE_W", 100), h = env_int("NEOTYPE_H", 30);
    int crashes = 0, pending = 0;
    if (w < MIN_W) w = MIN_W;
    if (h < MIN_H) h = MIN_H;

    rs = env_seed();
    for (int i = 0; i < 8; i++) rnd32();
    canvas_resize(w, h);
    game_set_viewport(w, h);
    game_set_state(S_MENU);

    int was_ready = 0;                 /* was the crash overlay a menu last frame? */
    for (int f = 0; f < frames; f++) {
        /* Fixed stimulus: start, a key every other frame, and a few one-off
         * control keys so the pause / clear / backspace paths get covered. */
        if      (f == 1)   game_on_key('\r');
        else if (f == 300) game_on_key('\t');
        else if (f == 360) game_on_key('\t');
        else if (f == 500) game_on_key('\r');
        else if (f == 520) game_on_key(127);

        if (f > 1 && !(f & 1)) game_on_key(next_key());

        /* Walk the crash path both ways. The first crash leaves the way a
         * player who dislikes the run would: back to the menu, caps off, play
         * on -- which records the game-over menu, S_OVER -> S_MENU, and the
         * lowercase rain that follows. The second crash retries with Enter,
         * covering the other exit. A crash lands wherever the RNG puts it, so
         * this triggers on the overlay settling rather than on a frame number;
         * cases need enough frames to reach a second crash for both to be
         * covered (see tests/run.sh). */
        int ready = game_over_t_reached(0.8f);
        int became_menu = ready && !was_ready;

        const Game *g = game();
        if (g->state == S_PLAY) game_update((float)FRAME);
        else if (g->state == S_OVER) game_advance_over((float)FRAME);
        draw_frame();

        /* Every 60 frames, plus the frame the crash overlay turns into a menu:
         * that swap is a rendering change, and when it happens depends on where
         * the RNG ends the run, so a fixed interval cannot sample it reliably. */
        if (f % 60 == 0 || f == frames - 1 || became_menu) digest(g, f);

        /* Act only after digesting, so the game-over menu is on record as it
         * was drawn before a keypress takes it away.
         *
         * The keys land a frame apart (pending counts them down) and each is
         * digested on arrival, because these screens are up for a single frame
         * and the fixed cadence would never see them. Batching the keys would
         * skip the menu altogether. */
        int moved = 0;
        if (became_menu) {
            if (crashes++ == 0) { game_on_key('2'); pending = 1; }  /* to the menu */
            else                 game_on_key('\r');                 /* retry */
            moved = 1;
        } else if (pending) {
            if (pending == 1) game_on_key('3');     /* caps off, on the menu */
            else              game_on_key('1');     /* play on, lowercase */
            if (++pending == 3) pending = 0;
            moved = 1;
        }
        was_ready = ready;

        if (moved) { draw_frame(); digest(g, f); }
    }
}

/* ---------- main ---------- */
int main(void) {
    if (getenv("NEOTYPE_HEADLESS")) { headless_run(); return 0; }
    if (!isatty(0) || !isatty(1)) { fputs("neotype needs an interactive terminal\n", stderr); return 1; }

    rs = env_seed();
    for (int i = 0; i < 8; i++) rnd32();

    /* Before term_init(): the audio stack probes the sound card and talks to
     * stderr, which is still an ordinary terminal at this point. With no
     * device, clear the menu setting too, so it shows off rather than on. */
    if (!sound_open()) {
        game()->sound = 0;
        fputs("neotype: no audio output device, playing silently\n", stderr);
    }

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
            sound_sync(g);
        }
        draw_frame();
        present();
    }
    sound_close();
    return 0;
}

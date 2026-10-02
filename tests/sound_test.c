/* sound_test.c - checks the game-event -> sound mapping in src/sound.c.
 *
 * Drives the real simulation (game.o) into the real mapping (sound.o) and
 * records which effect fired, by supplying audio_play() itself. Nothing links
 * audio.o, so this needs no sound card and does not even pull in miniaudio.
 *
 * What it pins down:
 *   - silence before the run starts (the snd_ready regression: a zeroed
 *     baseline is not a game state, because level starts at 1)
 *   - a kill is also a hit, so hit + kill == hits, with no phantom sounds
 *   - one game over per crash, and one level up per level change
 *   - retrying after a crash is silent
 *   - the menu's sound setting, both ways: muting is inert and tells the
 *     mixer, and unmuting resumes on a silent baseline rather than dumping
 *     the muted stretch's events at once
 */
#include "sound.h"

#include "audio.h"    /* Sfx + the audio_play() prototype we implement */
#include "game.h"
#include "util.h"

#include <stdio.h>

/* ---- recording seam ----
 *
 * The whole audio side is supplied here: audio_play() records, and
 * audio_init() reports whatever fake_device says. Nothing links audio.o, so
 * this needs no sound card and does not pull in miniaudio.
 */

static int fired[SFX_COUNT];
static int total, bad_id, bad_pitch, bad_gain;
static int fake_device;                  /* what audio_init() should report */
static int muted;                        /* what sound last asked the mixer for */

/* The menu setting decides whether sound is wanted; sound mirrors it onto
     * the mixer. Nothing links audio.o, so provide that seam too. */
void audio_play(Sfx id, float pitch, float gain) {
    if ((unsigned)id >= SFX_COUNT) { bad_id++; return; }
    fired[id]++;
    total++;
    if (!(pitch >= 0.25f && pitch <= 4.0f)) bad_pitch++;   /* audio_play clamps here */
    if (!(gain >= 0.0f && gain <= 1.0f))   bad_gain++;
}

void audio_set_muted(bool m) { muted = m; }
bool audio_init(void)     { return fake_device; }
void audio_shutdown(void) {}

static int fails;
static void check(int ok, const char *what, const char *detail) {
    if (!ok) { printf("  FAIL: %s -- %s\n", what, detail); fails++; }
}

/* ---- driver ---- */

/* Its own LCG, so the stimulus is identical across runs and RNG drift in the
 * sim cannot quietly change what is being tested. */
static uint64_t ks;
static char next_key(void) {
    static const char alpha[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    ks ^= ks << 13; ks ^= ks >> 7; ks ^= ks << 17;
    return alpha[(ks >> 11) % (sizeof alpha - 1)];
}

static int shots, hits, kills, level_jumps, crashes;

static void run(unsigned seed, int w, int h, unsigned frames, const char *label) {
    for (int i = 0; i < SFX_COUNT; i++) fired[i] = 0;
    total = bad_id = bad_pitch = bad_gain = 0;
    shots = hits = kills = level_jumps = crashes = 0;
    ks = 0x9e3779b97f4a7c15ULL ^ seed;

    rs = seed;
    for (int i = 0; i < 8; i++) rnd32();
    game_set_viewport(w, h);
    game_set_state(S_MENU);
    game_reset();

    /* sound_open() normally opens a device; the mapping is what is under
     * test, so drive it directly instead. */
    fake_device = 1;
    if (!sound_open()) { printf("  FAIL: sound_open did not open a present device\n"); fails++; }
    game()->sound = 1;

    int menu_sounds = 0, retry_sounds = 0;
    int p_shots = game()->shots, p_hits = game()->hits, p_kills = game()->kills;
    int p_level = game()->level, p_state = S_MENU;

    for (unsigned f = 0; f < frames; f++) {
        int base = total;

        if (f == 1) game_on_key('\r');                    /* start */
        else if (f == 300) game_on_key('\t');             /* pause */
        else if (f == 360) game_on_key('\t');             /* resume */
        else if (f > 1 && !(f & 1)) game_on_key(next_key());

        /* Retry once the crash overlay has settled, so the S_OVER -> S_PLAY
         * edge is covered as well as the crash itself. */
        if (!crashes && game_over_t_reached(1.0f)) game_on_key('\r');

        const Game *g = game();
        if (g->state == S_PLAY) game_update((float)FRAME);
        else if (g->state == S_OVER) game_advance_over((float)FRAME);

        sound_sync(g);

        /* Counters are zeroed by game_reset(); a drop is a reset, not a
         * negative delta, so skip it and carry on accumulating. */
        if (g->shots > p_shots) shots += g->shots - p_shots;
        if (g->hits  > p_hits)  hits  += g->hits  - p_hits;
        if (g->kills > p_kills) kills += g->kills - p_kills;
        if (g->level > p_level && g->state == p_state) level_jumps++;
        if (g->state == S_OVER && p_state != S_OVER) crashes++;
        if (p_state == S_OVER && g->state == S_PLAY && total > base) retry_sounds++;
        if (g->state == S_MENU && total > base) menu_sounds++;

        p_shots = g->shots; p_hits = g->hits; p_kills = g->kills;
        p_level = g->level; p_state = g->state;
    }

    char d[160];
    printf("%-14s shot=%-5d sim(hit=%-4d kill=%-3d) | sfx hit=%-4d miss=%-5d kill=%-3d"
           " levelup=%-2d gameover=%d crashes=%d\n",
           label, shots, hits, kills, fired[SFX_HIT], fired[SFX_MISS],
           fired[SFX_KILL], fired[SFX_LEVELUP], fired[SFX_GAMEOVER], crashes);

    /* This stimulus delivers one key per frame, so the per-frame cap never
     * engages and each effect should match its sim count exactly. The cap
     * itself is pinned separately by the burst test in main(). */
    snprintf(d, sizeof d, "%d kills fired vs %d sim kills", fired[SFX_KILL], kills);
    check(fired[SFX_KILL] == kills, "one kill sound per kill", d);
    snprintf(d, sizeof d, "%d hit sounds vs %d sim hits", fired[SFX_HIT], hits - kills);
    check(fired[SFX_HIT] == hits - kills, "one hit sound per non-kill hit", d);
    snprintf(d, sizeof d, "%d miss sounds vs %d sim misses", fired[SFX_MISS], shots - hits);
    check(fired[SFX_MISS] == shots - hits, "one miss sound per miss", d);
    snprintf(d, sizeof d, "%d fired vs %d crashes", fired[SFX_GAMEOVER], crashes);
    check(fired[SFX_GAMEOVER] == crashes, "one game over per crash", d);
    snprintf(d, sizeof d, "%d fired vs %d level changes", fired[SFX_LEVELUP], level_jumps);
    check(fired[SFX_LEVELUP] == level_jumps, "one level up per level change", d);
    snprintf(d, sizeof d, "%d sounds on the main menu", menu_sounds);
    check(menu_sounds == 0, "silent on the main menu", d);
    snprintf(d, sizeof d, "%d sounds while retrying", retry_sounds);
    check(retry_sounds == 0, "retry is silent", d);
    check(bad_id == 0, "no out-of-range Sfx", "");
    snprintf(d, sizeof d, "%d outside 0.25..4", bad_pitch);
    check(bad_pitch == 0, "pitch in range", d);
    snprintf(d, sizeof d, "%d outside 0..1", bad_gain);
    check(bad_gain == 0, "gain in range", d);
}

/* Deliver real shot events with the mapping live and record what appears.
 * A fresh game_reset() leaves no live drops, so every shot is a miss. */
static void reset_recording(void) {
    for (int i = 0; i < SFX_COUNT; i++) fired[i] = 0;
    total = 0;
}

static void burst_test(void) {
    char d[128];

    game_set_state(S_PLAY);
    game_reset();
    reset_recording();
    sound_sync(game());                       /* baseline for this run */

    for (int i = 0; i < 10; i++) game_shoot('a');
    reset_recording();
    sound_sync(game());
    snprintf(d, sizeof d, "%d of 10 misses sounded", fired[SFX_MISS]);
    check(fired[SFX_MISS] == 4, "per-frame cap holds at 4", d);

    /* And the cap must not eat ordinary play: one shot, one sound. */
    game_shoot('a');
    reset_recording();
    sound_sync(game());
    snprintf(d, sizeof d, "%d of 1 miss sounded", fired[SFX_MISS]);
    check(fired[SFX_MISS] == 1, "an ordinary miss still sounds", d);
}

int main(void) {
    run(1, 100, 30, 3600, "seed 1");
    run(2, 100, 30, 3600, "seed 2");
    run(6, 100, 30, 3600, "seed 6");
    run(1337, 100, 30, 3600, "seed 1337 (crash)");
    run(1, 94, 26, 3600, "small viewport");

    burst_test();

    /* No output device (SSH, container): sound_open reports the failure and the
     * mapping stays inert. A real shot is delivered, otherwise this would
     * pass vacuously. */
    fake_device = 0;
    check(!sound_open(), "no device: sound_open reports false", "");
    game()->sound = 1;                         /* the player still wants sound */
    sound_sync(game());                       /* sound_open cleared the ready flag */
    game_shoot('a');
    reset_recording();
    sound_sync(game());
    check(total == 0, "no device: nothing fires", "sound_sync fired anyway");
    sound_close();

    /* Muted via the menu, with a device present: inert, and the mixer is
     * told, so a voice already in flight is cut too. */
    fake_device = 1;
    sound_open();
    game()->sound = 0;
    muted = -1;
    sound_sync(game());
    check(muted == 1, "muted: mixer is told", "audio_set_muted not called with true");
    game_shoot('a');
    reset_recording();
    sound_sync(game());
    check(total == 0, "muted: nothing fires", "sound_sync fired anyway");

    /* Unmuting must not dump the muted interval's deltas all at once, so the
     * first sync after unmuting is a silent baseline. */
    game()->sound = 1;
    muted = -1;
    sound_sync(game());
    check(muted == 0, "unmuted: mixer is told", "audio_set_muted not called with false");
    reset_recording();
    sound_sync(game());
    check(total == 0, "unmuting is a silent baseline", "deltas dumped on unmute");

    /* ...but the next real event does sound. */
    game_shoot('a');
    reset_recording();
    sound_sync(game());
    check(total == 1, "sound resumes after unmuting", "still silent");

    if (fails) { printf("sound: FAILED (%d)\n", fails); return 1; }
    printf("sound: ok\n");
    return 0;
}
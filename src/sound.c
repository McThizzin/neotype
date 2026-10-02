#include "sound.h"

#include "audio.h"

/* A kill is also a hit (game_shoot bumps both counters), so hits split into
 * kills and the remainder. */
static struct { int shots, hits, kills, level, state; } last;
static bool ready;                 /* is `last` a real game state yet? */
static bool device;                /* did a device open? */

bool sound_open(void) {
    device = audio_init();
    ready = false;                 /* counters are per-run; resync on first sync */
    return device;
}

void sound_close(void) { audio_shutdown(); }

static void resync(const Game *g) {
    last.shots = g->shots; last.hits = g->hits;
    last.kills = g->kills; last.level = g->level;
    last.state = g->state;
}

void sound_sync(const Game *g) {
    /* The menu setting is the only source of truth. Mirroring it onto the
     * mixer also cuts any voice already in flight, so toggling mute is
     * immediate rather than after the longest effect finishes. */
    audio_set_muted(!g->sound);
    if (!device || !g->sound) return;

    /* A zeroed `last` is not a game state -- level starts at 1, not 0 -- so
     * adopt the opening state silently instead of reading the gap as a jump.
     * Also reached after unmuting, so toggling back on does not dump the
     * whole muted interval's worth of deltas at once. */
    if (!ready) { resync(g); ready = true; return; }

    if (g->state != last.state) {
        resync(g);
        if (g->state == S_OVER) audio_play(SFX_GAMEOVER, 1.0f, 0.6f);
        return;
    }

    int kills = g->kills - last.kills;
    int hits  = g->hits  - last.hits  - kills;
    int miss  = g->shots - last.shots - hits - kills;
    last.shots = g->shots; last.hits = g->hits; last.kills = g->kills;

    /* One keypress arrives per frame in practice, but a paste can deliver
     * several at once; cap so a burst cannot flood the request ring. */
    for (int i = 0; i < kills && i < 4; i++) audio_play(SFX_KILL, 1.0f, 0.8f);
    for (int i = 0; i < hits  && i < 4; i++) audio_play(SFX_HIT,  1.0f, 0.6f);
    for (int i = 0; i < miss  && i < 4; i++) audio_play(SFX_MISS, 1.0f, 0.5f);

    if (g->level > last.level) audio_play(SFX_LEVELUP, 1.0f, 0.7f);
    last.level = g->level;
}
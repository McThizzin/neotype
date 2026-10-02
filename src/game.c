#include "game.h"
#include "palette.h"
#include "util.h"

#include <math.h>
#include <string.h>

static Game g = { .level = 1, .state = S_TITLE };

/* The play field size. Owned by game rather than read from the canvas, so the
 * sim has no dependency on the renderer; main keeps the two in sync. */
static int VW = 80, VH = 24;

Game *game(void) { return &g; }

void game_set_viewport(int w, int h) { VW = w; VH = h; }

int game_quit_requested(void) { return g.quit_requested; }
void game_clear_quit(void) { g.quit_requested = 0; }

void game_set_state(int s) { g.state = s; }
int  game_drops_alive(void) { int n = 0; for (int i = 0; i < MAXD; i++) n += g.drops[i].alive != 0; return n; }
int  game_beams_live(void)  { int n = 0; for (int i = 0; i < MAXB; i++) n += g.beams[i].ttl > 0; return n; }
int  game_fx_live(void)     { int n = 0; for (int i = 0; i < MAXFX; i++) n += g.fxs[i].ttl > 0; return n; }
int  game_over_t_reached(float secs) { return g.state == S_OVER && g.over_t >= secs; }

void game_reset(void) {
    memset(g.drops, 0, sizeof g.drops);
    memset(g.beams, 0, sizeof g.beams);
    memset(g.fxs, 0, sizeof g.fxs);
    g.score = g.kills = g.combo = g.maxcombo = g.shots = g.hits = 0;
    g.level = 1;
    g.tlen = 0; memset(g.marks, 0, sizeof g.marks);
    g.spawn_t = 0.4f; g.miss_t = 0; g.over_t = 0; g.elapsed = 0;
}

static void spawn(void) {
    const int W = VW, H = VH;
    int slot = -1;
    for (int i = 0; i < MAXD; i++) if (!g.drops[i].alive) { slot = i; break; }
    if (slot < 0) return;
    int x = -1;
    for (int t = 0; t < 10 && x < 0; t++) {
        int cx = 1 + (int)(rnd32() % (uint32_t)(W - 2));
        int ok = 1;
        for (int i = 0; i < MAXD; i++)
            if (g.drops[i].alive && g.drops[i].x == cx && g.drops[i].y < 5) { ok = 0; break; }
        if (ok) x = cx;
    }
    if (x < 0) return;

    Drop *d = &g.drops[slot];
    d->alive = 1; d->x = x; d->y = 1; d->flash = 0;
    d->ch = (rndf() < 0.35f) ? (char)('A' + rnd32() % 26) : (char)('a' + rnd32() % 26);

    int p3 = imin(30, (g.level - 1) * 4);
    int p2 = imin(40, 8 + (g.level - 1) * 5);
    int roll = (int)(rnd32() % 100);
    d->hp = roll < p3 ? 3 : roll < p3 + p2 ? 2 : 1;
    d->maxhp = d->hp;

    float scale = clampf(H / 30.0f, 0.7f, 2.0f);        /* same fall time on any height */
    int lv = imin(g.level, 20);
    d->speed = (1.0f + 0.2f * lv + rndf() * 0.8f) * scale;
}

static void add_fx(float x, float y, float vx, float vy, char ch, int r, int gg, int b) {
    Fx *f = &g.fxs[g.fx_head++ % MAXFX];
    f->x = x; f->y = y; f->vx = vx; f->vy = vy; f->ttl = 0.28f; f->ch = ch;
    f->r = (uint8_t)r; f->g = (uint8_t)gg; f->b = (uint8_t)b;
}

static void add_beam(int x0, int y0, int x1, int y1) {
    for (int i = 0; i < MAXB; i++)
        if (g.beams[i].ttl <= 0) { g.beams[i] = (Beam){ x0, y0, x1, y1, 0.09f }; return; }
}

void game_shoot(char c) {
    const int W = VW, H = VH;
    if (g.tlen >= W - 6 || g.tlen >= MAX_MARKS - 2) g.tlen = 0;
    Mark *mk = &g.marks[g.tlen++];
    *mk = (Mark){ 0, 255, 70, 70, 0 };                 /* default: miss */
    g.shots++;

    int best = -1;
    for (int i = 0; i < MAXD; i++) {
        Drop *d = &g.drops[i];
        if (d->alive && d->ch == c && d->y >= 1 && (best < 0 || d->y > g.drops[best].y)) best = i;
    }
    if (best < 0) { g.combo = 0; g.miss_t = 0.18f; return; }

    Drop *d = &g.drops[best];
    int mr, mg, mb;
    hp_color(d->hp, &mr, &mg, &mb);                    /* colour of the target before this hit */
    *mk = (Mark){ 1, (uint8_t)mr, (uint8_t)mg, (uint8_t)mb, 0 };
    g.hits++; g.combo++;
    if (g.combo > g.maxcombo) g.maxcombo = g.combo;
    int mult = 1 + g.combo / 10;

    add_beam(2 + g.tlen - 1, H - 2, d->x, (int)d->y);
    d->hp--;
    d->flash = 0.08f;
    if (d->hp <= 0) {
        int r, gg, b;
        hp_color(d->maxhp, &r, &gg, &b);
        static const char sp[] = "*+.'`";
        for (int k = 0; k < 8; k++) {
            float a = (float)k * 0.785398f;
            add_fx((float)d->x, d->y, cosf(a) * 9.0f, sinf(a) * 4.5f, sp[k % 5], r, gg, b);
        }
        d->alive = 0;
        mk->kind = 2;
        g.kills++;
        g.score += 10 * d->maxhp * mult;
        g.level = 1 + g.kills / 12;
    } else {
        d->y -= 0.35f;                 /* little knock-back */
        if (d->y < 1) d->y = 1;
        g.score += mult;
    }
}

void game_update(float dt) {
    const int W = VW, H = VH;
    g.elapsed += dt;
    if (g.miss_t > 0) g.miss_t -= dt;
    for (int i = 0; i < g.tlen; i++) g.marks[i].age += dt;

    g.spawn_t -= dt;
    if (g.spawn_t <= 0) {
        spawn();
        float interval = fmaxf(0.22f, 1.0f - 0.06f * (float)(g.level - 1));
        interval *= clampf(80.0f / (float)W, 0.5f, 1.2f);   /* wider = more rain */
        g.spawn_t = interval * (0.6f + 0.8f * rndf());
    }

    for (int i = 0; i < MAXD; i++) {
        Drop *d = &g.drops[i];
        if (!d->alive) continue;
        if (d->x >= W - 1) d->x = W - 2;
        d->y += d->speed * dt;
        if (d->flash > 0) d->flash -= dt;
        if ((int)d->y >= H - 2) {          /* touched the command line */
            d->flash = 999;
            g.state = S_OVER;
            g.over_t = 0;
            return;
        }
    }
    for (int i = 0; i < MAXB; i++) if (g.beams[i].ttl > 0) g.beams[i].ttl -= dt;
    for (int i = 0; i < MAXFX; i++) {
        Fx *f = &g.fxs[i];
        if (f->ttl <= 0) continue;
        f->ttl -= dt; f->x += f->vx * dt; f->y += f->vy * dt;
    }
}

void game_advance_over(float dt) { g.over_t += dt; }

void game_on_key(int c) {
    if (c == 3) { g.quit_requested = 1; return; }
    int enter = (c == '\r' || c == '\n' || c == ' ');
    switch (g.state) {
    case S_TITLE:
        if (enter) { game_reset(); g.state = S_PLAY; }
        else if (c == 'q' || c == 'Q') g.quit_requested = 1;
        break;
    case S_OVER:
        if (g.over_t < 0.8f) break;               /* ignore keys mashed during the crash */
        if (enter) { game_reset(); g.state = S_PLAY; }
        else if (c == 'q' || c == 'Q') g.quit_requested = 1;
        break;
    case S_PAUSE:
        if (c == '\t' || enter) g.state = S_PLAY;
        break;
    case S_PLAY:
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) game_shoot((char)c);
        else if (c == '\t') g.state = S_PAUSE;
        else if (c == '\r' || c == '\n') g.tlen = 0;
        else if ((c == 127 || c == 8) && g.tlen > 0) g.tlen--;
        break;
    }
}

/*
 * neotype.c - matrix-rain typing shooter for the terminal
 *
 * Build:   make
 * Run:     ./neotype
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
 */
#define _POSIX_C_SOURCE 200809L

#include "canvas.h"
#include "term.h"
#include "util.h"

#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---------- TODO(step 4/5): game + draw are still inline below ---------- */
typedef struct { int x; float y, speed, flash; char ch; int hp, maxhp, alive; } Drop;
typedef struct { int x0, y0, x1, y1; float ttl; } Beam;
typedef struct { float x, y, vx, vy, ttl; char ch; uint8_t r, g, b; } Fx;
enum { S_TITLE, S_PLAY, S_PAUSE, S_OVER };

static Drop drops[MAXD];
static Beam beams[MAXB];
static Fx fxs[MAXFX];
static int fx_head;

static int state = S_TITLE;
static int score, kills, level = 1, combo, maxcombo, shots, hits;
static float spawn_t, miss_t, over_t;
static double elapsed;
typedef struct { uint8_t kind, r, g, b; float age; } Mark;   /* kind: 0 miss, 1 hit, 2 kill */
static Mark marks[512];
static int tlen;

/* ---------- game ---------- */
static void hp_color(int hp, int *r, int *g, int *b) {
    switch (hp) {
    case 1:  *r = 125;  *g = 230; *b = 93; break;   /* green */
    case 2:  *r = 251; *g = 155; *b = 50;  break;   /* amber */
    default: *r = 211; *g = 9;  *b = 82;  break;   /* red   */
    }
}

static void new_game(void) {
    memset(drops, 0, sizeof drops);
    memset(beams, 0, sizeof beams);
    memset(fxs, 0, sizeof fxs);
    score = kills = combo = maxcombo = shots = hits = 0;
    level = 1;
    tlen = 0; memset(marks, 0, sizeof marks);
    spawn_t = 0.4f; miss_t = 0; over_t = 0; elapsed = 0;
}

static int count_alive(void)     { int n = 0; for (int i = 0; i < MAXD; i++) n += drops[i].alive != 0; return n; }
static int count_live_beams(void) { int n = 0; for (int i = 0; i < MAXB; i++) n += beams[i].ttl > 0; return n; }
static int count_live_fx(void)   { int n = 0; for (int i = 0; i < MAXFX; i++) n += fxs[i].ttl > 0; return n; }

static void spawn(void) {
    const int W = canvas_w(), H = canvas_h();
    int slot = -1;
    for (int i = 0; i < MAXD; i++) if (!drops[i].alive) { slot = i; break; }
    if (slot < 0) return;
    int x = -1;
    for (int t = 0; t < 10 && x < 0; t++) {
        int cx = 1 + (int)(rnd32() % (uint32_t)(W - 2));
        int ok = 1;
        for (int i = 0; i < MAXD; i++)
            if (drops[i].alive && drops[i].x == cx && drops[i].y < 5) { ok = 0; break; }
        if (ok) x = cx;
    }
    if (x < 0) return;

    Drop *d = &drops[slot];
    d->alive = 1; d->x = x; d->y = 1; d->flash = 0;
    d->ch = (rndf() < 0.35f) ? (char)('A' + rnd32() % 26) : (char)('a' + rnd32() % 26);

    int p3 = imin(30, (level - 1) * 4);
    int p2 = imin(40, 8 + (level - 1) * 5);
    int roll = (int)(rnd32() % 100);
    d->hp = roll < p3 ? 3 : roll < p3 + p2 ? 2 : 1;
    d->maxhp = d->hp;

    float scale = clampf(H / 30.0f, 0.7f, 2.0f);        /* same fall time on any height */
    int lv = imin(level, 20);
    d->speed = (1.0f + 0.2f * lv + rndf() * 0.8f) * scale;
}

static void add_fx(float x, float y, float vx, float vy, char ch, int r, int g, int b) {
    Fx *f = &fxs[fx_head++ % MAXFX];
    f->x = x; f->y = y; f->vx = vx; f->vy = vy; f->ttl = 0.28f; f->ch = ch;
    f->r = (uint8_t)r; f->g = (uint8_t)g; f->b = (uint8_t)b;
}

static void add_beam(int x0, int y0, int x1, int y1) {
    for (int i = 0; i < MAXB; i++)
        if (beams[i].ttl <= 0) { beams[i] = (Beam){ x0, y0, x1, y1, 0.09f }; return; }
}

static void shoot(char c) {
    const int H = canvas_h();
    if (tlen >= canvas_w() - 6 || tlen >= (int)(sizeof marks / sizeof marks[0]) - 2) tlen = 0;
    Mark *mk = &marks[tlen++];
    *mk = (Mark){ 0, 255, 70, 70, 0 };                 /* default: miss */
    shots++;

    int best = -1;
    for (int i = 0; i < MAXD; i++) {
        Drop *d = &drops[i];
        if (d->alive && d->ch == c && d->y >= 1 && (best < 0 || d->y > drops[best].y)) best = i;
    }
    if (best < 0) { combo = 0; miss_t = 0.18f; return; }

    Drop *d = &drops[best];
    int mr, mg, mb;
    hp_color(d->hp, &mr, &mg, &mb);                    /* colour of the target before this hit */
    *mk = (Mark){ 1, (uint8_t)mr, (uint8_t)mg, (uint8_t)mb, 0 };
    hits++; combo++;
    if (combo > maxcombo) maxcombo = combo;
    int mult = 1 + combo / 10;

    add_beam(2 + tlen - 1, H - 2, d->x, (int)d->y);
    d->hp--;
    d->flash = 0.08f;
    if (d->hp <= 0) {
        int r, g, b;
        hp_color(d->maxhp, &r, &g, &b);
        static const char sp[] = "*+.'`";
        for (int k = 0; k < 8; k++) {
            float a = (float)k * 0.785398f;
            add_fx((float)d->x, d->y, cosf(a) * 9.0f, sinf(a) * 4.5f, sp[k % 5], r, g, b);
        }
        d->alive = 0;
        mk->kind = 2;
        kills++;
        score += 10 * d->maxhp * mult;
        level = 1 + kills / 12;
    } else {
        d->y -= 0.35f;                 /* little knock-back */
        if (d->y < 1) d->y = 1;
        score += mult;
    }
}

static void update(float dt) {
    const int W = canvas_w(), H = canvas_h();
    elapsed += dt;
    if (miss_t > 0) miss_t -= dt;
    for (int i = 0; i < tlen; i++) marks[i].age += dt;

    spawn_t -= dt;
    if (spawn_t <= 0) {
        spawn();
        float interval = fmaxf(0.22f, 1.0f - 0.06f * (float)(level - 1));
        interval *= clampf(80.0f / (float)W, 0.5f, 1.2f);   /* wider = more rain */
        spawn_t = interval * (0.6f + 0.8f * rndf());
    }

    for (int i = 0; i < MAXD; i++) {
        Drop *d = &drops[i];
        if (!d->alive) continue;
        if (d->x >= W - 1) d->x = W - 2;
        d->y += d->speed * dt;
        if (d->flash > 0) d->flash -= dt;
        if ((int)d->y >= H - 2) {          /* touched the command line */
            d->flash = 999;
            state = S_OVER;
            over_t = 0;
            return;
        }
    }
    for (int i = 0; i < MAXB; i++) if (beams[i].ttl > 0) beams[i].ttl -= dt;
    for (int i = 0; i < MAXFX; i++) {
        Fx *f = &fxs[i];
        if (f->ttl <= 0) continue;
        f->ttl -= dt; f->x += f->vx * dt; f->y += f->vy * dt;
    }
}

/* ---------- rendering ---------- */
static void draw_beam(const Beam *b) {
    int vx = b->x1 - b->x0, vy = b->y1 - b->y0;
    int ax = abs(vx), ay = abs(vy) * 2;            /* cells are ~2x taller than wide */
    uint32_t ch;
    if (ax * 2 < ay) ch = 0x2502;                   /* │ */
    else if (ay * 2 < ax) ch = 0x2500;              /* ─ */
    else ch = (vx * vy > 0) ? 0x2572 : 0x2571;      /* ╲ ╱ */

    float k = 0.5f + 0.5f * (b->ttl / 0.09f);
    int r = (int)(110 * k), g = (int)(230 * k), bl = (int)(255 * k);

    int x = b->x0, y = b->y0;
    int dx = abs(b->x1 - x), dy = -abs(b->y1 - y);
    int sx = x < b->x1 ? 1 : -1, sy = y < b->y1 ? 1 : -1, err = dx + dy;
    for (int guard = 0; guard < 1000; guard++) {
        if (x == b->x1 && y == b->y1) break;
        put(x, y, ch, r, g, bl, 1, 0);
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

static void overlay_box(int w, int h) {
    int x0 = (canvas_w() - w) / 2, y0 = (canvas_h() - h) / 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t ch = ' ';
            if (y == 0 && x == 0) ch = 0x250C;
            else if (y == 0 && x == w - 1) ch = 0x2510;
            else if (y == h - 1 && x == 0) ch = 0x2514;
            else if (y == h - 1 && x == w - 1) ch = 0x2518;
            else if (y == 0 || y == h - 1) ch = 0x2500;
            else if (x == 0 || x == w - 1) ch = 0x2502;
            put(x0 + x, y0 + y, ch, 70, 255, 110, 0, 0);
        }
}

static void render(void) {
    const int W = canvas_w(), H = canvas_h();
    canvas_clear();
    if (canvas_too_small()) {
        str_at(1, 1, "terminal too small", 255, 70, 70, 1, 0);
        char b[48]; snprintf(b, sizeof b, "need %dx%d, have %dx%d", MIN_W, MIN_H, W, H);
        str_at(1, 2, b, 200, 200, 200, 0, 0);
        return;
    }

    /* HUD */
    char buf[160];
    str_at(0, 0, " NEOTYPE ", 70, 255, 110, 1, 1);
    int acc = shots ? hits * 100 / shots : 100;
    snprintf(buf, sizeof buf, " score %d   level %d   streak %d (x%d)   acc %d%%", score, level, combo, 1 + combo / 10, acc);
    str_at(10, 0, buf, 200, 220, 200, 0, 0);
    if (W >= 80) {
        int x = W - 24, r, g, b;
        for (int hp = 1; hp <= 3; hp++) {
            hp_color(hp, &r, &g, &b);
            put(x, 0, 0x25A0, r, g, b, 1, 0);
            snprintf(buf, sizeof buf, " %d", hp);
            x = str_at(x + 1, 0, buf, 160, 160, 160, 0, 0) + 2;
        }
        str_at(x - 1, 0, "hits", 110, 110, 110, 0, 0);
    }

    /* danger line + prompt */
    for (int x = 0; x < W; x++) put(x, H - 2, 0x2500, 140, 40, 40, 0, 0);
    str_at(0, H - 1, "$ ", 70, 255, 110, 1, 0);
    for (int i = 0; i < tlen; i++) {
        const Mark *m = &marks[i];
        float k = clampf(1.0f - m->age / 4.0f, 0.12f, 1.0f);   /* fade over ~4s */
        uint32_t ch = m->kind == 2 ? 0x25C6 : m->kind == 1 ? 0x2022 : 0xD7;   /* ◆ • × */
        put(2 + i, H - 1, ch, (int)(m->r * k), (int)(m->g * k), (int)(m->b * k), m->kind == 2, 0);
    }
    if (miss_t > 0) put(2 + tlen, H - 1, ' ', 255, 70, 70, 0, 1);
    else            put(2 + tlen, H - 1, ' ', 70, 255, 110, 0, 1);

    /* rain trails (digits/symbols only, so letters are always targets) */
    static const char glyphs[] = "0123456789:=+*<>~";
    int ng = (int)sizeof glyphs - 1;
    for (int i = 0; i < MAXD; i++) {
        Drop *d = &drops[i];
        if (!d->alive) continue;
        int head = (int)d->y, r, g, b;
        hp_color(d->hp, &r, &g, &b);
        int len = 3 + d->maxhp * 2;
        for (int k = 1; k <= len; k++) {
            int row = head - k;
            if (row < 1) break;
            if (row >= H - 2) continue;
            float f = 0.38f * (1.0f - (float)(k - 1) / (float)len);
            int gi = (int)(((unsigned)d->x * 7u + (unsigned)row * 13u + (unsigned)(elapsed * 6.0)) % (unsigned)ng);
            put(d->x, row, (uint32_t)glyphs[gi], (int)(r * f), (int)(g * f), (int)(b * f), 0, 0);
        }
    }

    for (int i = 0; i < MAXB; i++) if (beams[i].ttl > 0) draw_beam(&beams[i]);

    for (int i = 0; i < MAXD; i++) {
        Drop *d = &drops[i];
        if (!d->alive) continue;
        int row = (int)d->y, r, g, b;
        if (row < 1 || row >= H - 1) continue;
        if (d->flash > 0) { r = g = b = 255; } else hp_color(d->hp, &r, &g, &b);
        put(d->x, row, (uint32_t)d->ch, r, g, b, 1, 0);
    }

    for (int i = 0; i < MAXFX; i++) {
        Fx *f = &fxs[i];
        if (f->ttl <= 0) continue;
        float k = f->ttl / 0.28f;
        int y = (int)lroundf(f->y);
        if (y < 1 || y >= H - 2) continue;
        put((int)lroundf(f->x), y, (uint32_t)f->ch, (int)(f->r * k), (int)(f->g * k), (int)(f->b * k), 1, 0);
    }

    /* overlays */
    if (state == S_TITLE) {
        overlay_box(52, 12);
        int y0 = (H - 12) / 2;
        center(y0 + 2, "N E O T Y P E", 70, 255, 110, 1);
        center(y0 + 4, "type the falling letters before they", 200, 200, 200, 0);
        center(y0 + 5, "touch your prompt.  case matters: a != A", 200, 200, 200, 0);
        int r, g, b, x = (W - 33) / 2;
        for (int hp = 1; hp <= 3; hp++) {
            hp_color(hp, &r, &g, &b);
            put(x, y0 + 7, 0x25A0, r, g, b, 1, 0);
            snprintf(buf, sizeof buf, " %d hit%s", hp, hp > 1 ? "s" : "");
            x = str_at(x + 1, y0 + 7, buf, 160, 160, 160, 0, 0) + 3;
        }
        center(y0 + 9, "enter start    tab pause    esc quit", 120, 120, 120, 0);
    } else if (state == S_PAUSE) {
        overlay_box(20, 5);
        center(H / 2, "P A U S E D", 255, 200, 40, 1);
    } else if (state == S_OVER) {
        overlay_box(36, 11);
        int y0 = (H - 11) / 2;
        center(y0 + 2, "G A M E   O V E R", 255, 70, 70, 1);
        snprintf(buf, sizeof buf, "score     %d", score);        center(y0 + 4, buf, 230, 230, 230, 1);
        snprintf(buf, sizeof buf, "level     %d", level);        center(y0 + 5, buf, 200, 200, 200, 0);
        snprintf(buf, sizeof buf, "best run  %d", maxcombo);     center(y0 + 6, buf, 200, 200, 200, 0);
        snprintf(buf, sizeof buf, "accuracy  %d%%", acc);        center(y0 + 7, buf, 200, 200, 200, 0);
        center(y0 + 9, over_t < 0.8f ? "..." : "enter retry    q quit", 120, 120, 120, 0);
    }
}

/* ---------- input ---------- */
static void on_key(int c) {
    if (c == 3) { quit_flag = 1; return; }
    int enter = (c == '\r' || c == '\n' || c == ' ');
    switch (state) {
    case S_TITLE:
        if (enter) { new_game(); state = S_PLAY; }
        else if (c == 'q' || c == 'Q') quit_flag = 1;
        break;
    case S_OVER:
        if (over_t < 0.8f) break;               /* ignore keys mashed during the crash */
        if (enter) { new_game(); state = S_PLAY; }
        else if (c == 'q' || c == 'Q') quit_flag = 1;
        break;
    case S_PAUSE:
        if (c == '\t' || enter) state = S_PLAY;
        break;
    case S_PLAY:
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) shoot((char)c);
        else if (c == '\t') state = S_PAUSE;
        else if (c == '\r' || c == '\n') tlen = 0;
        else if ((c == 127 || c == 8) && tlen > 0) tlen--;
        break;
    }
}

static void read_input(void) {
    unsigned char b[256];
    ssize_t n = read(0, b, sizeof b);
    if (n == 0) { quit_flag = 1; return; }
    if (n < 0) return;
    for (ssize_t i = 0; i < n; i++) {
        int c = b[i];
        if (c == 27) {
            if (i + 1 >= n) { quit_flag = 1; return; }        /* lone Esc */
            ssize_t j = i + 1;                                /* skip escape sequence */
            if (b[j] == '[' || b[j] == 'O') {
                j++;
                while (j < n && !(b[j] >= 0x40 && b[j] <= 0x7e)) j++;
            }
            i = j;
            continue;
        }
        on_key(c);
    }
}

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
    state = S_TITLE;

    for (int f = 0; f < frames; f++) {
        /* Fixed stimulus: start, a key every other frame, and a few one-off
         * control keys so the pause / clear / backspace paths get covered. */
        if      (f == 1)   on_key('\r');
        else if (f == 300) on_key('\t');
        else if (f == 360) on_key('\t');
        else if (f == 500) on_key('\r');
        else if (f == 520) on_key(127);

        if (f > 1 && !(f & 1)) on_key(next_key());

        /* Once the crash overlay has settled, retry once, so new_game() and the
         * S_OVER -> S_PLAY transition are covered as well. */
        if (!retried && state == S_OVER && over_t >= 1.0f) { on_key('\r'); retried = 1; }

        if (state == S_PLAY) update((float)FRAME);
        else if (state == S_OVER) over_t += (float)FRAME;
        render();

        if (f % 60 == 0 || f == frames - 1)
            printf("f=%04d state=%d score=%d kills=%d level=%d shots=%d hits=%d "
                   "combo=%d maxcombo=%d tlen=%d elapsed=%.3f "
                   "drops=%d beams=%d fx=%d rng=%016llx grid=%016llx\n",
                   f, state, score, kills, level, shots, hits, combo, maxcombo,
                   tlen, elapsed, count_alive(), count_live_beams(), count_live_fx(),
                   (unsigned long long)rs, (unsigned long long)canvas_hash());
    }
}

/* ---------- main ---------- */
int main(void) {
    if (getenv("NEOTYPE_HEADLESS")) { headless_run(); return 0; }
    if (!isatty(0) || !isatty(1)) { fputs("neotype needs an interactive terminal\n", stderr); return 1; }

    rs = env_seed();
    for (int i = 0; i < 8; i++) rnd32();

    term_init();

    int w, h;
    term_size(&w, &h);
    canvas_resize(w, h);

    double last = now(), next = last;
    while (!quit_flag) {
        double t0 = now(), wait = next - t0;
        if (wait > 0) {
            struct pollfd p = { 0, POLLIN, 0 };
            int pr = poll(&p, 1, (int)(wait * 1000.0) + 1);
            if (pr > 0 && (p.revents & (POLLIN | POLLHUP))) read_input();
        }
        t0 = now();
        if (t0 < next) continue;

        double dt = t0 - last;
        last = t0;
        if (dt > 0.1) dt = 0.1;
        next = t0 + FRAME;

        term_size(&w, &h);
        if (w != canvas_w() || h != canvas_h()) canvas_resize(w, h);

        if (!canvas_too_small()) {
            if (state == S_PLAY) update((float)dt);
            else if (state == S_OVER) over_t += (float)dt;
        }
        render();
        present();
    }
    return 0;
}

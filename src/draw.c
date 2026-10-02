#include "draw.h"

#include "canvas.h"
#include "game.h"
#include "palette.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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
            put(x0 + x, y0 + y, ch, C_ACCENT_R, C_ACCENT_G, C_ACCENT_B, 0, 0);
        }
}

static void draw_too_small(int W, int H) {
    str_at(1, 1, "terminal too small", C_BAD_R, C_BAD_G, C_BAD_B, 1, 0);
    char b[48];
    snprintf(b, sizeof b, "need %dx%d, have %dx%d", MIN_W, MIN_H, W, H);
    str_at(1, 2, b, C_TEXT_R, C_TEXT_G, C_TEXT_B, 0, 0);
}

static void draw_hud(const Game *g, int W) {
    char buf[160];
    str_at(0, 0, " NEOTYPE ", C_ACCENT_R, C_ACCENT_G, C_ACCENT_B, 1, 1);
    int acc = g->shots ? g->hits * 100 / g->shots : 100;
    snprintf(buf, sizeof buf, " score %d   level %d   streak %d (x%d)   acc %d%%",
             g->score, g->level, g->combo, 1 + g->combo / 10, acc);
    str_at(10, 0, buf, 200, 220, 200, 0, 0);
    if (W >= 80) {
        int x = W - 24, r, gr, b;
        for (int hp = 1; hp <= 3; hp++) {
            hp_color(hp, &r, &gr, &b);
            put(x, 0, 0x25A0, r, gr, b, 1, 0);
            snprintf(buf, sizeof buf, " %d", hp);
            x = str_at(x + 1, 0, buf, C_LABEL_R, C_LABEL_G, C_LABEL_B, 0, 0) + 2;
        }
        str_at(x - 1, 0, "hits", C_MUTED_R, C_MUTED_G, C_MUTED_B, 0, 0);
    }
}

/* the red danger line plus the prompt with its per-shot marks */
static void draw_prompt(const Game *g, int W, int H) {
    for (int x = 0; x < W; x++) put(x, H - 2, 0x2500, C_WARN_R, C_WARN_G, C_WARN_B, 0, 0);
    str_at(0, H - 1, "$ ", C_ACCENT_R, C_ACCENT_G, C_ACCENT_B, 1, 0);
    for (int i = 0; i < g->tlen; i++) {
        const Mark *m = &g->marks[i];
        float k = clampf(1.0f - m->age / 4.0f, 0.12f, 1.0f);   /* fade over ~4s */
        uint32_t ch = m->kind == 2 ? 0x25C6 : m->kind == 1 ? 0x2022 : 0xD7;   /* ◆ • × */
        put(2 + i, H - 1, ch, (int)(m->r * k), (int)(m->g * k), (int)(m->b * k), m->kind == 2, 0);
    }
    if (g->miss_t > 0) put(2 + g->tlen, H - 1, ' ', C_BAD_R, C_BAD_G, C_BAD_B, 0, 1);
    else               put(2 + g->tlen, H - 1, ' ', C_ACCENT_R, C_ACCENT_G, C_ACCENT_B, 0, 1);
}

/* rain trails (digits/symbols only, so letters are always targets) */
static void draw_rain_trails(const Game *g, int H) {
    static const char glyphs[] = "0123456789:=+*<>~";
    int ng = (int)sizeof glyphs - 1;
    for (int i = 0; i < MAXD; i++) {
        const Drop *d = &g->drops[i];
        if (!d->alive) continue;
        int head = (int)d->y, r, gr, b;
        hp_color(d->hp, &r, &gr, &b);
        int len = 3 + d->maxhp * 2;
        for (int k = 1; k <= len; k++) {
            int row = head - k;
            if (row < 1) break;
            if (row >= H - 2) continue;
            float f = 0.38f * (1.0f - (float)(k - 1) / (float)len);
            int gi = (int)(((unsigned)d->x * 7u + (unsigned)row * 13u + (unsigned)(g->elapsed * 6.0)) % (unsigned)ng);
            put(d->x, row, (uint32_t)glyphs[gi], (int)(r * f), (int)(gr * f), (int)(b * f), 0, 0);
        }
    }
}

static void draw_beams(const Game *g) {
    for (int i = 0; i < MAXB; i++) if (g->beams[i].ttl > 0) draw_beam(&g->beams[i]);
}

static void draw_letters(const Game *g, int H) {
    for (int i = 0; i < MAXD; i++) {
        const Drop *d = &g->drops[i];
        if (!d->alive) continue;
        int row = (int)d->y, r, gr, b;
        if (row < 1 || row >= H - 1) continue;
        if (d->flash > 0) { r = gr = b = 255; } else hp_color(d->hp, &r, &gr, &b);
        put(d->x, row, (uint32_t)d->ch, r, gr, b, 1, 0);
    }
}

static void draw_fx(const Game *g, int H) {
    for (int i = 0; i < MAXFX; i++) {
        const Fx *f = &g->fxs[i];
        if (f->ttl <= 0) continue;
        float k = f->ttl / 0.28f;
        int y = (int)lroundf(f->y);
        if (y < 1 || y >= H - 2) continue;
        put((int)lroundf(f->x), y, (uint32_t)f->ch,
            (int)(f->r * k), (int)(f->g * k), (int)(f->b * k), 1, 0);
    }
}

/* One menu line: a number key, a label, and a value. The key is what you
 * press, so it carries the accent; the value stays quiet. */
static void menu_item(int y, const char *key, const char *label, const char *value) {
    int x = (canvas_w() - 28) / 2;
    str_at(x, y, key, C_ACCENT_R, C_ACCENT_G, C_ACCENT_B, 1, 0);
    str_at(x + 3, y, label, C_TEXT_R, C_TEXT_G, C_TEXT_B, 0, 0);
    if (value && *value)
        str_at(x + 22, y, value, C_DIM_R, C_DIM_G, C_DIM_B, 0, 0);
}

static void overlay_menu(void) {
    const Game *g = game();
    char buf[160];
    overlay_box(52, 16);
    int y0 = (canvas_h() - 16) / 2;

    center(y0 + 1, "N E O T Y P E", C_ACCENT_R, C_ACCENT_G, C_ACCENT_B, 1);
    center(y0 + 3, "type the falling letters before they", C_TEXT_R, C_TEXT_G, C_TEXT_B, 0);
    center(y0 + 4, "touch your prompt.  case matters: a != A", C_TEXT_R, C_TEXT_G, C_TEXT_B, 0);

    int r, gr, b, x = (canvas_w() - 33) / 2;
    for (int hp = 1; hp <= 3; hp++) {
        hp_color(hp, &r, &gr, &b);
        put(x, y0 + 6, 0x25A0, r, gr, b, 1, 0);
        snprintf(buf, sizeof buf, " %d hit%s", hp, hp > 1 ? "s" : "");
        x = str_at(x + 1, y0 + 6, buf, C_LABEL_R, C_LABEL_G, C_LABEL_B, 0, 0) + 3;
    }

    menu_item(y0 + 8,  "1", "start", NULL);
    menu_item(y0 + 9,  "2", "sound", g->sound ? "on" : "off");
    menu_item(y0 + 10, "3", "caps",  g->caps  ? "on" : "off");
    center(y0 + 12, "esc quit", C_DIM_R, C_DIM_G, C_DIM_B, 0);
}

static void overlay_pause(void) {
    overlay_box(20, 5);
    center(canvas_h() / 2, "P A U S E D", C_PAUSE_R, C_PAUSE_G, C_PAUSE_B, 1);
}

static void overlay_over(const Game *g) {
    int H = canvas_h();
    char buf[160];
    overlay_box(36, 13);
    int y0 = (H - 13) / 2;
    int acc = g->shots ? g->hits * 100 / g->shots : 100;
    center(y0 + 1, "G A M E   O V E R", C_BAD_R, C_BAD_G, C_BAD_B, 1);
    snprintf(buf, sizeof buf, "score     %d", g->score);     center(y0 + 3, buf, C_BRIGHT_R, C_BRIGHT_G, C_BRIGHT_B, 1);
    snprintf(buf, sizeof buf, "level     %d", g->level);     center(y0 + 4, buf, C_TEXT_R, C_TEXT_G, C_TEXT_B, 0);
    snprintf(buf, sizeof buf, "best run  %d", g->maxcombo);  center(y0 + 5, buf, C_TEXT_R, C_TEXT_G, C_TEXT_B, 0);
    snprintf(buf, sizeof buf, "accuracy  %d%%", acc);         center(y0 + 6, buf, C_TEXT_R, C_TEXT_G, C_TEXT_B, 0);
    if (g->over_t < 0.8f) { center(y0 + 9, "...", C_DIM_R, C_DIM_G, C_DIM_B, 0); return; }
    menu_item(y0 + 9,  "1", "retry",      NULL);
    menu_item(y0 + 10, "2", "main menu",  NULL);
    center(y0 + 11, "esc quit", C_DIM_R, C_DIM_G, C_DIM_B, 0);
}

void draw_frame(void) {
    const Game *g = game();
    const int W = canvas_w(), H = canvas_h();

    canvas_clear();
    if (canvas_too_small()) { draw_too_small(W, H); return; }

    draw_hud(g, W);
    draw_prompt(g, W, H);
    draw_rain_trails(g, H);
    draw_beams(g);
    draw_letters(g, H);
    draw_fx(g, H);

    if      (g->state == S_MENU)  overlay_menu();
    else if (g->state == S_PAUSE) overlay_pause();
    else if (g->state == S_OVER)  overlay_over(g);
}

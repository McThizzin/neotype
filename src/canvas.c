#include "canvas.h"
#include "term.h"
#include "util.h"

#include <stdlib.h>
#include <unistd.h>

static int W, H, need_clear = 1;
static Cell *cur, *prev;

int canvas_w(void) { return W; }
int canvas_h(void) { return H; }
int canvas_too_small(void) { return W < MIN_W || H < MIN_H; }

void canvas_clear(void) {
    for (int i = 0; i < W * H; i++) { cur[i].ch = ' '; cur[i].r = cur[i].g = cur[i].b = 0; cur[i].bold = cur[i].inv = 0; }
}

void put(int x, int y, uint32_t ch, int r, int g, int b, int bold, int inv) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    Cell *c = &cur[y * W + x];
    c->ch = ch; c->r = (uint8_t)r; c->g = (uint8_t)g; c->b = (uint8_t)b;
    c->bold = (uint8_t)bold; c->inv = (uint8_t)inv;
}

int str_at(int x, int y, const char *s, int r, int g, int b, int bold, int inv) {
    while (*s) put(x++, y, u8next(&s), r, g, b, bold, inv);
    return x;
}

void center(int y, const char *s, int r, int g, int b, int bold) {
    str_at((W - ulen(s)) / 2, y, s, r, g, b, bold, 0);
}

void canvas_resize(int w, int h) {
    W = w; H = h;
    free(cur); free(prev);
    cur = calloc((size_t)W * H, sizeof(Cell));
    prev = calloc((size_t)W * H, sizeof(Cell));
    if (!cur || !prev) _exit(1);
    for (int i = 0; i < W * H; i++) prev[i].ch = 0xFFFFFFFFu;
    need_clear = 1;
}

static int cell_eq(const Cell *a, const Cell *b) {
    return a->ch == b->ch && a->r == b->r && a->g == b->g && a->b == b->b &&
           a->bold == b->bold && a->inv == b->inv;
}

/* diff the grid against what's on screen and emit only the changes */
void present(void) {
    if (need_clear) { ob_put("\x1b[2J", 4); need_clear = 0; }
    size_t start = ob_len();
    ob_put("\x1b[?2026h", 8);
    size_t after_hdr = ob_len();
    int cx = -1, cy = -1, sr = -1, sg = -1, sb = -1, sbold = -1, sinv = -1;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            Cell *c = &cur[y * W + x], *p = &prev[y * W + x];
            if (cell_eq(c, p)) continue;
            *p = *c;
            if (cx != x || cy != y) OBF("\x1b[%d;%dH", y + 1, x + 1);
            int blank = (c->ch == ' ' || c->ch == 0) && !c->inv;
            if (blank) {
                if (sinv == 1) { ob_put("\x1b[0m", 4); sinv = 0; sbold = 0; sr = sg = sb = -1; }
            } else if (c->r != sr || c->g != sg || c->b != sb || c->bold != sbold || c->inv != sinv) {
                OBF("\x1b[0%s%s;38;2;%d;%d;%dm", c->bold ? ";1" : "", c->inv ? ";7" : "", c->r, c->g, c->b);
                sr = c->r; sg = c->g; sb = c->b; sbold = c->bold; sinv = c->inv;
            }
            ob_utf8(c->ch ? c->ch : ' ');
            cx = x + 1; cy = y;
        }
    }
    if (ob_len() == after_hdr) { ob_truncate(start); return; }   /* nothing changed */
    ob_put("\x1b[?2026l", 8);
    flush_ob();
}

uint64_t canvas_hash(void) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < W * H; i++) {
        const Cell *c = &cur[i];
        const unsigned char f[9] = {
            (unsigned char)(c->ch & 0xFF), (unsigned char)((c->ch >> 8) & 0xFF),
            (unsigned char)((c->ch >> 16) & 0xFF), (unsigned char)((c->ch >> 24) & 0xFF),
            c->r, c->g, c->b, c->bold, c->inv
        };
        for (int k = 0; k < 9; k++) { h ^= f[k]; h *= 1099511628211ULL; }
    }
    return h;
}

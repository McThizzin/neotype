/* canvas.h - a diffable grid of styled cells. Knows nothing about the game:
 * callers paint into it, then present() emits only what changed. */
#pragma once

#include <stdint.h>

typedef struct { uint32_t ch; uint8_t r, g, b, bold, inv; } Cell;

void canvas_resize(int w, int h);
int  canvas_w(void);
int  canvas_h(void);
int  canvas_too_small(void);        /* below MIN_W x MIN_H */

void canvas_clear(void);
void put(int x, int y, uint32_t ch, int r, int g, int b, int bold, int inv);
int  str_at(int x, int y, const char *s, int r, int g, int b, int bold, int inv);
void center(int y, const char *s, int r, int g, int b, int bold);

/* emit the diff to stdout; no-op when nothing changed */
void present(void);

/* hash of the current grid, for the headless regression harness */
uint64_t canvas_hash(void);

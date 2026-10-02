/* util.h - pure helpers: rng, math, clock, utf-8. No dependencies. */
#pragma once

#include <stdint.h>

#define FRAME 0.016
#define MAXD 512
#define MAXB 16
#define MAXFX 256
#define MIN_W 94
#define MIN_H 26

/* xorshift32; seeded from NEOTYPE_SEED when set, else time/pid. */
extern uint64_t rs;
uint32_t rnd32(void);
float rndf(void);

int   imin(int a, int b);
float clampf(float v, float lo, float hi);
double now(void);

/* decode one UTF-8 code point, advancing *p. Invalid bytes -> U+FFFD. */
uint32_t u8next(const char **p);
int ulen(const char *s);

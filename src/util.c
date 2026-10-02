#define _POSIX_C_SOURCE 200809L
#include "util.h"

#include <stdlib.h>
#include <time.h>

uint64_t rs = 88172645463325252ULL;

uint32_t rnd32(void) {
    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
    return (uint32_t)(rs >> 11);
}
float rndf(void) { return (rnd32() & 0xFFFFFF) / 16777216.0f; }
int   imin(int a, int b) { return a < b ? a : b; }
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

uint32_t u8next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = s[0];
    int n = 1;
    if (c < 0x80) n = 1;
    else if ((c >> 5) == 6)  { c &= 0x1f; n = 2; }
    else if ((c >> 4) == 14) { c &= 0x0f; n = 3; }
    else c = 0xFFFD;
    for (int i = 1; i < n; i++) c = (c << 6) | (s[i] & 0x3f);
    *p += n;
    return c;
}
int ulen(const char *s) { int n = 0; while (*s) { u8next(&s); n++; } return n; }

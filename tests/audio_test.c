/* audio_test.c - checks the procedural sound engine.
 *
 * Never opens a sound card: it drives the same device-free mixer the game
 * links against (audio_mixer_init / audio_mix), so it runs in a container or
 * over SSH where there is no output device.
 *
 *   tests/audio_test            assertions only
 *   tests/audio_test /tmp/snd   also write one .wav per effect, to listen to
 *
 * The directory must already exist; the Makefile target does not create it
 * because a missing path is far more likely a typo than an intended output
 * location. Pass OUT=dir to `make test-audio` to get one made for you.
 */
#include "audio.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static const char *const names[SFX_COUNT] = {
    "hit", "miss", "kill", "levelup", "gameover"
};

/* 16-bit mono PCM. Returns 0 on success. */
static int wav(const char *path, const float *s, unsigned n) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 0; }

    uint32_t rate = AUDIO_RATE, bytes = n * 2, riff = 36 + bytes, fmt = 16, br = rate * 2;
    uint16_t pcm = 1, ch = 1, align = 2, bits = 16;

    int ok = fwrite("RIFF", 1, 4, f) == 4 &&
             fwrite(&riff, 4, 1, f) == 1 &&
             fwrite("WAVEfmt ", 1, 8, f) == 8 &&
             fwrite(&fmt, 4, 1, f) == 1 &&
             fwrite(&pcm, 2, 1, f) == 1 &&
             fwrite(&ch, 2, 1, f) == 1 &&
             fwrite(&rate, 4, 1, f) == 1 &&
             fwrite(&br, 4, 1, f) == 1 &&
             fwrite(&align, 2, 1, f) == 1 &&
             fwrite(&bits, 2, 1, f) == 1 &&
             fwrite("data", 1, 4, f) == 4 &&
             fwrite(&bytes, 4, 1, f) == 1;

    for (unsigned i = 0; ok && i < n; i++) {
        int16_t v = (int16_t)(s[i] * 32767.0f);
        ok = fwrite(&v, 2, 1, f) == 1;
    }
    if (fclose(f) != 0) ok = 0;
    if (!ok) fprintf(stderr, "%s: write failed\n", path);
    return ok;
}

static float peak(const float *s, unsigned n, int *bad) {
    float p = 0;
    for (unsigned i = 0; i < n; i++) {
        if (!isfinite(s[i])) (*bad)++;
        if (fabsf(s[i]) > p) p = fabsf(s[i]);
    }
    return p;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : NULL;
    unsigned N = AUDIO_RATE * 2;
    float *buf = malloc(N * sizeof *buf);
    int fails = 0;

    if (!buf) { puts("FAIL: out of memory"); return 1; }
    audio_mixer_init();

    for (int id = 0; id < SFX_COUNT; id++) {
        audio_play((Sfx)id, 1.0f, 1.0f);
        audio_mix(buf, N);

        int bad = 0;
        float p = peak(buf, N, &bad);
        unsigned last = 0;
        for (unsigned i = 0; i < N; i++) if (fabsf(buf[i]) > 0.001f) last = i;

        printf("%-9s peak %.2f  nan/inf %d  audible %.0f ms\n",
               names[id], p, bad, 1000.0 * last / AUDIO_RATE);
        /* Normalised buffers mean every effect should be clearly audible but
         * leave headroom, so 16-voice stacking cannot clip. */
        if (bad || p < 0.3f || p > 1.0f) fails++;

        if (out) {
            char path[512];
            snprintf(path, sizeof path, "%s/%s.wav", out, names[id]);
            if (!wav(path, buf, last + 1)) fails++;
        }
    }

    /* silence when nothing is queued */
    audio_mix(buf, 512);
    int bad = 0;
    if (peak(buf, 512, &bad) != 0) { puts("FAIL: not silent when idle"); fails++; }

    /* flood: ring overflow + voice stealing must not crash or exceed full scale */
    bad = 0;
    for (int i = 0; i < 500; i++) audio_play((Sfx)(i % SFX_COUNT), 0.5f + (i % 7) * 0.3f, 1.0f);
    float worst = 0;
    for (int blk = 0; blk < 200; blk++) {
        audio_mix(buf, 441);
        float p = peak(buf, 441, &bad);
        if (p > worst) worst = p;
    }
    printf("flood     peak %.2f  nan/inf %d\n", worst, bad);
    if (bad || worst > 1.0f) fails++;

    /* muted: nothing gets queued */
    audio_set_muted(true);
    audio_play(SFX_KILL, 1.0f, 1.0f);
    audio_mix(buf, 4410);
    if (peak(buf, 4410, &bad) != 0) { puts("FAIL: muted but audible"); fails++; }
    audio_set_muted(false);

    free(buf);
    if (fails) printf("audio: FAILED (%d)\n", fails);
    else       printf("audio: ok\n");
    return fails != 0;
}

/* audio.c - tiny procedural sound engine.
 *
 *   recipes  -> synthesised once at startup into short float buffers
 *   audio_play() -> pushes a request into a lock-free ring
 *   audio_mix()  -> (audio thread) drains the ring, mixes active voices
 *
 * No sample files: everything is generated, so the game stays one binary.
 */
#include "audio.h"

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RATE        AUDIO_RATE
#define MAX_VOICES  16
#define RING        64              /* must be a power of two */
#define TWO_PI      6.28318530718f

/* ---------- synthesis ---------- */

typedef struct { float *data; int len; } Buf;

/* One swept oscillator plus filtered noise under an exponential decay. */
typedef struct {
    float f0, f1;      /* start / end frequency in Hz (exponential sweep)   */
    float dur;         /* seconds                                           */
    float tone;        /* oscillator level                                  */
    float square;      /* 0 = pure sine, 1 = square (grittier)              */
    float noise;       /* noise level                                       */
    float decay;       /* envelope speed; higher = shorter, snappier        */
} Recipe;

typedef struct { Recipe r; float start; } Note;

/* ---- the sounds: tweak these ---- */
static const Note N_HIT[]  = { {{1500,  800, .09f, 1.0f, .45f, .10f,  8.0f}, 0} };  /* bright blip */
static const Note N_MISS[] = { {{ 150,   70, .13f, 1.0f, .00f, .45f, 10.0f}, 0} };  /* dull thud   */
static const Note N_KILL[] = { {{ 700,  110, .30f,  .6f, .30f, .90f,  8.0f}, 0} };  /* crunchy pop */
static const Note N_LVL[]  = {                                                      /* arpeggio    */
    {{ 523,  523, .14f, 1.0f, .25f, 0, 12.0f}, .00f},
    {{ 659,  659, .14f, 1.0f, .25f, 0, 12.0f}, .07f},
    {{ 784,  784, .14f, 1.0f, .25f, 0, 12.0f}, .14f},
    {{1047, 1047, .25f, 1.0f, .25f, 0,  8.0f}, .21f},
};
static const Note N_OVER[] = { {{ 440,   55, .90f, 1.0f, .50f, .15f,  3.5f}, 0} };  /* falling    */

static const struct { const Note *notes; int count; } DEF[SFX_COUNT] = {
    [SFX_HIT]      = { N_HIT,  1 },
    [SFX_MISS]     = { N_MISS, 1 },
    [SFX_KILL]     = { N_KILL, 1 },
    [SFX_LEVELUP]  = { N_LVL,  4 },
    [SFX_GAMEOVER] = { N_OVER, 1 },
};

static void render_note(float *d, int total, const Recipe *r, float start) {
    int off = (int)(start * RATE), n = (int)(r->dur * RATE);
    int att = (int)(0.002f * RATE), rel = (int)(0.006f * RATE);   /* de-click */
    float ph = 0, lp = 0;
    uint32_t rng = 0x1234567u + (uint32_t)off;

    for (int i = 0; i < n && off + i < total; i++) {
        float t = (float)i / (float)n;
        float f = r->f0 * powf(r->f1 / r->f0, t);
        ph += f / (float)RATE;
        ph -= floorf(ph);

        float sn = sinf(TWO_PI * ph);
        float sq = ph < 0.5f ? 0.6f : -0.6f;
        float osc = (1.0f - r->square) * sn + r->square * sq;

        rng = rng * 1664525u + 1013904223u;
        float nz = (float)(rng >> 8) / 8388608.0f - 1.0f;
        lp += 0.3f * (nz - lp);                                    /* one-pole low-pass */

        float env = expf(-t * r->decay);
        if (i < att)     env *= (float)i / (float)att;
        if (n - i < rel) env *= (float)(n - i) / (float)rel;

        d[off + i] += (r->tone * osc + r->noise * 1.6f * lp) * env;
    }
}

static Buf build(const Note *notes, int count) {
    float total = 0;
    for (int i = 0; i < count; i++) total = fmaxf(total, notes[i].start + notes[i].r.dur);
    int n = (int)(total * RATE) + 1;
    float *d = calloc((size_t)n, sizeof *d);
    if (!d) return (Buf){ NULL, 0 };

    for (int i = 0; i < count; i++) render_note(d, n, &notes[i].r, notes[i].start);

    float peak = 1e-6f;                                            /* normalise: loudness is */
    for (int i = 0; i < n; i++) peak = fmaxf(peak, fabsf(d[i]));   /* controlled by `gain`   */
    for (int i = 0; i < n; i++) d[i] *= 0.9f / peak;
    return (Buf){ d, n };
}

/* ---------- mixer ---------- */

typedef struct { Sfx id; float pitch, gain; } Req;
typedef struct { const Buf *buf; float pos, step, gain; } Voice;

static Buf bufs[SFX_COUNT];
static bool built;

static Req ring[RING];                    /* single producer (game), single consumer (audio) */
static atomic_uint ring_w, ring_r;

static Voice voices[MAX_VOICES];          /* touched only by the audio thread */
static atomic_bool muted_flag;

void audio_mixer_init(void) {
    if (built) return;
    for (int i = 0; i < SFX_COUNT; i++) bufs[i] = build(DEF[i].notes, DEF[i].count);
    built = true;
}

static void start_voice(const Req *q) {
    if (!bufs[q->id].data) return;
    int slot = 0;
    float worst = -1.0f;
    for (int i = 0; i < MAX_VOICES; i++) {                 /* free slot, else steal the oldest */
        if (!voices[i].buf) { slot = i; break; }
        float prog = voices[i].pos / (float)voices[i].buf->len;
        if (prog > worst) { worst = prog; slot = i; }
    }
    float step = q->pitch < 0.25f ? 0.25f : q->pitch > 4.0f ? 4.0f : q->pitch;
    voices[slot] = (Voice){ &bufs[q->id], 0.0f, step, q->gain };
}

void audio_mix(float *out, unsigned frames) {
    memset(out, 0, frames * sizeof *out);
    if (!built) return;

    unsigned r = atomic_load_explicit(&ring_r, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&ring_w, memory_order_acquire);
    while (r != w) start_voice(&ring[r++ % RING]);
    atomic_store_explicit(&ring_r, r, memory_order_release);

    for (int v = 0; v < MAX_VOICES; v++) {
        Voice *vo = &voices[v];
        if (!vo->buf) continue;
        const Buf *b = vo->buf;
        for (unsigned i = 0; i < frames; i++) {
            int k = (int)vo->pos;
            if (k + 1 >= b->len) { vo->buf = NULL; break; }
            float fr = vo->pos - (float)k;
            out[i] += (b->data[k] * (1.0f - fr) + b->data[k + 1] * fr) * vo->gain;
            vo->pos += vo->step;
        }
    }
    for (unsigned i = 0; i < frames; i++) out[i] = tanhf(out[i] * 0.7f);   /* soft clip */
}

void audio_play(Sfx id, float pitch, float gain) {
    if (!built || (unsigned)id >= SFX_COUNT || atomic_load(&muted_flag)) return;
    unsigned w = atomic_load_explicit(&ring_w, memory_order_relaxed);
    unsigned r = atomic_load_explicit(&ring_r, memory_order_acquire);
    if (w - r >= RING) return;                              /* full: drop the sound, never block */
    ring[w % RING] = (Req){ id, pitch, gain };
    atomic_store_explicit(&ring_w, w + 1, memory_order_release);
}

void audio_set_muted(bool m) { atomic_store(&muted_flag, m); }
bool audio_is_muted(void)    { return atomic_load(&muted_flag); }

/* ---------- device (miniaudio) ---------- */

#ifdef AUDIO_NO_DEVICE

bool audio_init(void)     { return false; }
void audio_shutdown(void) {}

#else

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "vendor/miniaudio.h"    /* path-qualified, so check-layers.sh
                                  * reads it as external, not a local module */

#include <fcntl.h>
#include <unistd.h>

/* ALSA / JACK print probe errors straight to stderr ("cannot find card 0" ...),
 * which would scribble over the TUI. Mute stderr while talking to the audio
 * stack. Call audio_init() before the game takes over the terminal. */
static int quiet_begin(void) {
    int saved = dup(2), nul = open("/dev/null", O_WRONLY);
    if (nul >= 0) { dup2(nul, 2); close(nul); }
    return saved;
}
static void quiet_end(int saved) {
    if (saved >= 0) { dup2(saved, 2); close(saved); }
}

static ma_device device;
static bool device_open;

static void on_audio(ma_device *d, void *out, const void *in, ma_uint32 frames) {
    (void)d; (void)in;
    audio_mix((float *)out, frames);
}

bool audio_init(void) {
    audio_mixer_init();

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = 1;
    cfg.sampleRate        = RATE;
    cfg.dataCallback      = on_audio;
    cfg.periodSizeInMilliseconds = 10;                      /* low latency */

    int q = quiet_begin();
    bool ok = ma_device_init(NULL, &cfg, &device) == MA_SUCCESS;
    if (ok && ma_device_start(&device) != MA_SUCCESS) { ma_device_uninit(&device); ok = false; }
    quiet_end(q);
    device_open = ok;
    return ok;
}

void audio_shutdown(void) {
    if (!device_open) return;
    int q = quiet_begin();
    ma_device_uninit(&device);
    quiet_end(q);
    device_open = false;
}

#endif

/*
 * sfx.c — A small synthesiser for Kurukshetra.
 *
 * Instruments (all written into float buffers at 44.1 kHz):
 *   thump   pitch-swept sine: dhol, nagara, footfalls
 *   noise   filtered noise bursts: slaps, dust, breath, the "shing" of steel
 *   metal   inharmonic partials with separate decays: swords, bells, mail
 *   pluck   Karplus-Strong string: the tanpura drone
 *   horn    additive harmonics with glide + vibrato: shehnai, conch, war horn
 *
 * Music loops are written with wrap-around, so notes ringing past the end of
 * the loop continue at its start and the loop is seamless.
 */
#include "sfx.h"

#include "raylib.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "res/conch_mp3.h" /* recorded conch (shankh), embedded */

#define SR 44100
#define TAU 6.28318531f

/* ------------------------------------------------------------------------ */
/* Buffers and helpers                                                      */
/* ------------------------------------------------------------------------ */

typedef struct {
    float *s;
    int    n;
    bool   wrap; /* music loops wrap; one-shots clip at the end */
} Buf;

static Buf buf_new(float seconds, bool wrap)
{
    Buf b = { NULL, (int)(seconds * SR), wrap };
    b.s = calloc((size_t)b.n, sizeof(float));
    return b;
}

static inline void put(Buf *b, long i, float v)
{
    if (b->wrap)
        i %= b->n;
    else if (i >= b->n)
        return;
    if (i >= 0)
        b->s[i] += v;
}

static uint32_t noise_state = 0x1234567u;
static inline float noise(void)
{
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    return (float)(noise_state >> 8) / 8388608.0f - 1.0f;
}
static float frand(float lo, float hi) { return lo + (hi - lo) * (noise() * 0.5f + 0.5f); }

/* Sine by table lookup: `cycles` is the phase in turns (any size). */
#define SINE_N 4096
static float sine_table[SINE_N + 1];

static void sine_init(void)
{
    for (int i = 0; i <= SINE_N; i++)
        sine_table[i] = sinf(TAU * i / SINE_N);
}

static inline float fsin(float cycles)
{
    float x = (cycles - floorf(cycles)) * SINE_N;
    int   i = (int)x;
    return sine_table[i] + (sine_table[i + 1] - sine_table[i]) * (x - i);
}

/* Pitch-swept sine: f(t) = f_end + (f_start - f_end) * e^(-sweep t). */
static void thump(Buf *b, float t0, float f_start, float f_end, float sweep,
                  float decay, float amp, float dur)
{
    long start = (long)(t0 * SR);
    int  len   = (int)(dur * SR);
    float phase = 0, sw = 1.0f, env = 1.0f;
    const float sw_step = expf(-sweep / SR), env_step = expf(-decay / SR);
    for (int i = 0; i < len; i++) {
        float t = (float)i / SR;
        phase += (f_end + (f_start - f_end) * sw) / SR;
        put(b, start + i, fsin(phase) * env * fminf(1.0f, t * 800.0f) * amp);
        sw *= sw_step;
        env *= env_step;
    }
}

/*
 * Filtered noise. `lp` (0..1) is a one-pole low-pass coefficient; with
 * `highpass` the low-passed part is subtracted instead (bright, airy noise).
 */
static void noise_burst(Buf *b, float t0, float dur, float amp, float attack,
                        float decay, float lp, bool highpass)
{
    long start = (long)(t0 * SR);
    int  len   = (int)(dur * SR);
    float y = 0, env = 1.0f;
    const float env_step = expf(-decay / SR);
    for (int i = 0; i < len; i++) {
        float t = (float)i / SR;
        float x = noise();
        y += lp * (x - y);
        float v = highpass ? x - y : y;
        put(b, start + i, v * env * fminf(1.0f, t / attack) * amp);
        env *= env_step;
    }
}

/* Sum of inharmonic sine partials, each with its own decay (metal, bells). */
static void metal(Buf *b, float t0, float dur, const float *freq, const float *amps,
                  const float *decays, int n, float amp)
{
    long start = (long)(t0 * SR);
    int  len   = (int)(dur * SR);
    for (int k = 0; k < n; k++) {
        float step = freq[k] / SR, ph = frand(0, 1), env = amps[k] * amp;
        const float env_step = expf(-decays[k] / SR);
        for (int i = 0; i < len; i++) {
            float attack = fminf(1.0f, (float)i / SR * 3000.0f);
            put(b, start + i, fsin(ph) * env * attack);
            ph += step;
            if (ph > 1.0f)
                ph -= 1.0f;
            env *= env_step;
        }
    }
}

/* Karplus-Strong plucked string: a noise burst circulating in a delay line. */
static void pluck(Buf *b, float t0, float freq, float dur, float amp, float damping)
{
    int period = (int)(SR / freq);
    float *line = malloc(sizeof(float) * (size_t)period);
    float y = 0;
    for (int i = 0; i < period; i++) { /* slightly low-passed excitation */
        y += 0.5f * (noise() - y);
        line[i] = y;
    }
    long start = (long)(t0 * SR);
    int  len   = (int)(dur * SR), idx = 0;
    for (int i = 0; i < len; i++) {
        int next = (idx + 1) % period;
        float out = line[idx];
        line[idx] = damping * 0.5f * (line[idx] + line[next]);
        /* A touch of "jawari" buzz: soft asymmetric saturation. */
        float buzz = out + 0.35f * out * fabsf(out);
        float fade = i > len - 2000 ? (float)(len - i) / 2000.0f : 1.0f;
        put(b, start + i, buzz * amp * fade);
        idx = next;
    }
    free(line);
}

/*
 * Reed / brass voice: harmonics 1..10 shaped by `bright` and a nasal bump
 * around the 3rd-5th harmonic, a glide from `f_from` (meend), vibrato after
 * the onset, and some breath noise.
 */
static void horn(Buf *b, float t0, float dur, float freq, float f_from, float glide,
                 float amp, float attack, float release, float bright, float vib,
                 float breath)
{
    float weights[10], total = 0;
    for (int k = 0; k < 10; k++) {
        float nasal = (k >= 2 && k <= 4) ? 1.6f : 1.0f;
        weights[k] = powf(bright, (float)k) * nasal;
        total += weights[k];
    }
    long start = (long)(t0 * SR);
    int  len   = (int)(dur * SR);
    float phase = 0, by = 0;
    for (int i = 0; i < len; i++) {
        float t = (float)i / SR;
        float g = glide > 0 ? fminf(1.0f, t / glide) : 1.0f;
        float f = f_from + (freq - f_from) * (1.0f - (1.0f - g) * (1.0f - g));
        float v_on = fminf(1.0f, fmaxf(0.0f, (t - 0.18f) * 3.0f));
        f *= 1.0f + vib * v_on * fsin(5.2f * t);
        phase += f / SR;
        if (phase > 1.0f)
            phase -= 1.0f;

        float s = 0;
        for (int k = 0; k < 10; k++)
            s += weights[k] * fsin(phase * (k + 1));
        s /= total;
        by += 0.08f * (noise() - by);

        float env = fminf(1.0f, t / attack);
        if (t > dur - release)
            env *= fmaxf(0.0f, (dur - t) / release);
        put(b, start + i, (s + by * breath) * env * amp);
    }
}

/* ------------------------------------------------------------------------ */
/* Instruments built from the voices                                        */
/* ------------------------------------------------------------------------ */

static void dhol_bass(Buf *b, float t, float amp)
{
    thump(b, t, 118, 58, 22, 9, amp, 0.5f);
    noise_burst(b, t, 0.03f, amp * 0.25f, 0.001f, 90, 0.3f, false);
}

static void dhol_slap(Buf *b, float t, float amp)
{
    noise_burst(b, t, 0.12f, amp * 0.9f, 0.001f, 38, 0.35f, true);
    thump(b, t, 330, 250, 30, 26, amp * 0.45f, 0.15f);
}

static void nagara(Buf *b, float t, float amp)
{
    thump(b, t, 72, 38, 6, 3.2f, amp, 1.6f);
    noise_burst(b, t, 0.6f, amp * 0.25f, 0.002f, 6, 0.03f, false);
}

static void clash(Buf *b, float t, float amp, float base)
{
    const float ratios[6] = { 1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 3.91f };
    const float amps[6]   = { 0.50f, 0.45f, 0.35f, 0.25f, 0.14f, 0.30f };
    const float decays[6] = { 3.2f, 4.8f, 7.0f, 10.0f, 14.0f, 6.0f };
    float f[6];
    for (int k = 0; k < 6; k++)
        f[k] = base * ratios[k] * frand(0.985f, 1.015f);
    metal(b, t, 1.6f, f, amps, decays, 6, amp);
    noise_burst(b, t, 0.04f, amp * 1.1f, 0.0005f, 110, 0.25f, true);   /* impact */
    noise_burst(b, t + 0.05f, 0.3f, amp * 0.35f, 0.01f, 12, 0.2f, true); /* shing */
    for (int k = 0; k < 6; k++)                                        /* rebound */
        f[k] *= 1.07f;
    metal(b, t + 0.07f, 1.0f, f, amps, decays, 6, amp * 0.35f);
}

static void temple_bell(Buf *b, float t, float amp, float base)
{
    const float f[5] = { base, base * 2.0f, base * 2.76f, base * 4.1f, base * 5.4f };
    const float a[5] = { 0.6f, 0.4f, 0.3f, 0.2f, 0.12f };
    const float d[5] = { 1.2f, 1.8f, 2.6f, 3.5f, 5.0f };
    metal(b, t, 3.0f, f, a, d, 5, amp);
}

static void conch(Buf *b, float t, float dur, float amp)
{
    horn(b, t, dur, 220, 160, 0.45f, amp, 0.30f, 0.45f, 0.78f, 0.010f, 0.35f);
    horn(b, t, dur, 110, 82, 0.45f, amp * 0.45f, 0.35f, 0.45f, 0.7f, 0.010f, 0.1f);
    noise_burst(b, t, dur, amp * 0.12f, 0.2f, 1.2f, 0.05f, false); /* breath */
}

/* ------------------------------------------------------------------------ */
/* Converting buffers into raylib objects                                   */
/* ------------------------------------------------------------------------ */

static int16_t *to_pcm(Buf *b, float peak)
{
    float max = 1e-6f;
    for (int i = 0; i < b->n; i++)
        max = fmaxf(max, fabsf(b->s[i]));
    float gain = peak / max;
    int16_t *pcm = malloc(sizeof(int16_t) * (size_t)b->n);
    for (int i = 0; i < b->n; i++)
        pcm[i] = (int16_t)(b->s[i] * gain * 32767.0f);
    return pcm;
}

static Sound to_sound(Buf *b, float peak)
{
    int16_t *pcm = to_pcm(b, peak);
    Wave w = { (unsigned)b->n, SR, 16, 1, pcm };
    Sound s = LoadSoundFromWave(w); /* copies */
    free(pcm);
    free(b->s);
    return s;
}

/* Music streams from an in-memory WAV file, which must stay allocated. */
static unsigned char *music_wav[MUSIC_COUNT];

static void le32(unsigned char *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i)); }
static void le16(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }

static Music to_music(Buf *b, MusicId id)
{
    int16_t *pcm = to_pcm(b, 0.8f);
    uint32_t data = (uint32_t)b->n * 2;
    unsigned char *wav = malloc(44 + data);
    memcpy(wav, "RIFF", 4);  le32(wav + 4, 36 + data);
    memcpy(wav + 8, "WAVEfmt ", 8);
    le32(wav + 16, 16); le16(wav + 20, 1); le16(wav + 22, 1);   /* PCM, mono */
    le32(wav + 24, SR); le32(wav + 28, SR * 2); le16(wav + 32, 2); le16(wav + 34, 16);
    memcpy(wav + 36, "data", 4); le32(wav + 40, data);
    memcpy(wav + 44, pcm, data);
    free(pcm);
    free(b->s);
    music_wav[id] = wav;
    return LoadMusicStreamFromMemory(".wav", wav, (int)(44 + data));
}

/* ------------------------------------------------------------------------ */
/* Sound effects                                                            */
/* ------------------------------------------------------------------------ */

static Sound make_sfx(SfxId id)
{
    Buf b;
    switch (id) {
    case SFX_STEP:
        b = buf_new(0.16f, false);
        noise_burst(&b, 0, 0.12f, 0.6f, 0.002f, 38, 0.10f, false);
        thump(&b, 0, 120, 60, 40, 32, 0.4f, 0.14f);
        return to_sound(&b, 0.55f);
    case SFX_ENEMY_STEP: {
        b = buf_new(0.35f, false);
        thump(&b, 0, 88, 45, 25, 18, 0.9f, 0.3f);
        noise_burst(&b, 0, 0.12f, 0.4f, 0.002f, 30, 0.08f, false);
        const float f[4] = { 3150, 4420, 5980, 7340 }, a[4] = { .12f, .1f, .08f, .06f },
                    d[4] = { 40, 45, 50, 55 };
        metal(&b, 0.01f, 0.2f, f, a, d, 4, 1.0f);   /* chain mail */
        metal(&b, 0.045f, 0.2f, f, a, d, 4, 0.7f);
        return to_sound(&b, 0.7f);
    }
    case SFX_BLOCKED: {
        b = buf_new(0.2f, false);
        const float f[3] = { 190, 430, 760 }, a[3] = { .5f, .3f, .15f }, d[3] = { 30, 40, 55 };
        metal(&b, 0, 0.2f, f, a, d, 3, 1.0f);
        noise_burst(&b, 0, 0.03f, 0.3f, 0.001f, 100, 0.3f, false);
        return to_sound(&b, 0.5f);
    }
    case SFX_CLASH:
        b = buf_new(1.8f, false);
        clash(&b, 0, 1.0f, 640);
        return to_sound(&b, 0.85f);
    case SFX_CLASH_HEAVY:
        b = buf_new(2.2f, false);
        clash(&b, 0, 1.0f, 560);
        clash(&b, 0.13f, 0.9f, 700);
        thump(&b, 0.13f, 90, 40, 10, 5, 1.0f, 1.0f);
        return to_sound(&b, 0.95f);
    case SFX_DRUM_ROLL: {
        b = buf_new(2.4f, false);
        float t = 0, gap = 0.16f;
        for (int i = 0; i < 12; i++) {
            (i % 2 ? dhol_slap : dhol_bass)(&b, t, 0.5f + i * 0.04f);
            t += gap;
            gap *= 0.86f;
        }
        nagara(&b, t + 0.05f, 1.0f);
        noise_burst(&b, 0, 2.2f, 0.35f, 0.4f, 1.6f, 0.015f, false); /* earth rumble */
        return to_sound(&b, 0.9f);
    }
    case SFX_CONCH:
        b = buf_new(2.6f, false);
        conch(&b, 0, 2.5f, 1.0f);
        return to_sound(&b, 0.8f);
    case SFX_VICTORY: {
        /* Drums and bells only: the recorded conch is played alongside. */
        b = buf_new(3.6f, false);
        const float beats[] = { 0, 0.18f, 0.36f, 0.54f, 0.72f, 0.81f, 0.9f };
        for (int i = 0; i < 7; i++)
            (i % 3 == 1 ? dhol_slap : dhol_bass)(&b, 1.6f + beats[i], 0.7f);
        nagara(&b, 2.6f, 1.0f);
        temple_bell(&b, 2.6f, 0.4f, 880);
        return to_sound(&b, 0.9f);
    }
    case SFX_DEFEAT:
        b = buf_new(3.4f, false);
        clash(&b, 0, 1.0f, 560);
        horn(&b, 0.35f, 1.9f, 110, 147, 1.5f, 0.8f, 0.2f, 0.6f, 0.72f, 0.006f, 0.2f);
        nagara(&b, 0.35f, 0.9f);
        nagara(&b, 1.4f, 0.7f);
        return to_sound(&b, 0.9f);
    case SFX_CAPTURE:
        b = buf_new(1.8f, false);
        thump(&b, 0, 160, 70, 25, 16, 0.9f, 0.4f);
        clash(&b, 0, 0.4f, 320);
        temple_bell(&b, 0.08f, 0.6f, 988);
        return to_sound(&b, 0.8f);
    case SFX_ENEMY_GATE:
        b = buf_new(1.2f, false);
        horn(&b, 0, 1.0f, 110, 92, 0.2f, 0.9f, 0.08f, 0.3f, 0.75f, 0.004f, 0.25f);
        nagara(&b, 0, 0.6f);
        return to_sound(&b, 0.7f);
    case SFX_UI_MOVE: {
        b = buf_new(0.1f, false);
        const float f[2] = { 1400, 2900 }, a[2] = { .5f, .25f }, d[2] = { 60, 90 };
        metal(&b, 0, 0.1f, f, a, d, 2, 1.0f);
        return to_sound(&b, 0.35f);
    }
    case SFX_UI_SELECT: {
        b = buf_new(0.6f, false);
        for (int i = 0; i < 5; i++) { /* ghungroo: a cluster of tiny bells */
            float base = frand(2600, 4200);
            const float a[3] = { .5f, .3f, .2f }, d[3] = { 14, 18, 24 };
            float f[3] = { base, base * 1.51f, base * 2.13f };
            metal(&b, i * 0.03f + frand(0, 0.02f), 0.45f, f, a, d, 3, 0.6f);
        }
        return to_sound(&b, 0.5f);
    }
    case SFX_DICE: {
        b = buf_new(0.8f, false);
        float t = 0;
        for (int i = 0; i < 8; i++) {
            float base = frand(1700, 2600);
            const float a[2] = { .5f, .3f }, d[2] = { 70, 90 };
            float f[2] = { base, base * 2.3f };
            metal(&b, t, 0.08f, f, a, d, 2, 1.0f);
            noise_burst(&b, t, 0.02f, 0.4f, 0.0005f, 150, 0.3f, true);
            t += frand(0.03f, 0.1f);
        }
        return to_sound(&b, 0.6f);
    }
    case SFX_DANGER:
        b = buf_new(0.6f, false);
        thump(&b, 0, 64, 40, 12, 12, 1.0f, 0.3f);
        thump(&b, 0.22f, 60, 38, 12, 14, 0.7f, 0.3f);
        return to_sound(&b, 0.7f);
    default:
        b = buf_new(0.05f, false);
        return to_sound(&b, 0.1f);
    }
}

/* ------------------------------------------------------------------------ */
/* Music                                                                    */
/* ------------------------------------------------------------------------ */

/* Melody phrases in semitones above Sa, with lengths in beats (2 bars each). */
typedef struct { float semi, beats; } MNote;
static const MNote PHRASE[4][8] = {
    { { 0, 2 }, { 1, 0.5f }, { 3, 0.5f }, { 5, 1 }, { 7, 2 }, { 5, 1 }, { 3, 1 }, { -99, 0 } },
    { { 7, 1 }, { 8, 1 }, { 10, 1 }, { 8, 0.5f }, { 7, 0.5f }, { 5, 2 }, { 3, 1 }, { 1, 1 } },
    { { 12, 2 }, { 10, 1 }, { 8, 1 }, { 7, 2 }, { 8, 0.5f }, { 7, 0.5f }, { 5, 1 }, { -99, 0 } },
    { { 3, 1 }, { 5, 1 }, { 3, 0.5f }, { 1, 0.5f }, { 0, 3 }, { -2, 1 }, { 0, 1 }, { -99, 0 } },
};

static float play_phrase(Buf *b, int p, float t, float beat, float sa, float amp,
                         float bright, float breath, float prev)
{
    for (int i = 0; i < 8 && PHRASE[p][i].semi > -90; i++) {
        float f   = sa * powf(2.0f, PHRASE[p][i].semi / 12.0f);
        float dur = PHRASE[p][i].beats * beat;
        horn(b, t, dur * 0.97f, f, prev > 0 ? prev : f, 0.07f, amp, 0.05f, 0.09f,
             bright, 0.012f, breath);
        prev = f;
        t += dur;
    }
    return prev;
}

/* Frequency nudged so a whole number of cycles fits the loop (no click). */
static float loop_freq(float f, float loop_seconds)
{
    return roundf(f * loop_seconds) / loop_seconds;
}

static void drone(Buf *b, float loop_seconds, float sa, float amp)
{
    float f1 = loop_freq(sa / 2, loop_seconds), f2 = loop_freq(sa * 0.75f, loop_seconds);
    float lfo = loop_freq(0.25f, loop_seconds);
    for (int i = 0; i < b->n; i++) {
        float t = (float)i / SR;
        float trem = 0.75f + 0.25f * fsin(lfo * t);
        float v = fsin(f1 * t) + 0.5f * fsin(2 * f1 * t) + 0.6f * fsin(f2 * t);
        b->s[i] += v * trem * amp;
    }
}

static void tanpura(Buf *b, float loop_seconds, float cycle, float sa, float amp)
{
    const float notes[4] = { 0.75f, 1.0f, 1.0f, 0.5f }; /* Pa, Sa, Sa, low Sa */
    int plucks = (int)(loop_seconds / (cycle / 4));
    for (int i = 0; i < plucks; i++)
        pluck(b, i * cycle / 4, sa * notes[i % 4], cycle * 1.6f, amp, 0.9985f);
}

static Music make_battle_music(void)
{
    const float bpm = 132, beat = 60 / bpm, bar = beat * 4, step = beat / 4;
    const int   bars = 16;
    const float len = bar * bars, sa = 146.83f; /* D */
    Buf b = buf_new(len, true);

    drone(&b, len, sa, 0.05f);
    tanpura(&b, len, bar / 2, sa, 0.22f);

    for (int bi = 0; bi < bars; bi++) {
        float t0 = bi * bar;
        bool fill = bi % 4 == 3;
        const int bass[] = { 0, 3, 6, 10, 11 };
        for (int k = 0; k < 5; k++)
            dhol_bass(&b, t0 + bass[k] * step, 0.8f);
        dhol_slap(&b, t0 + 4 * step, 0.55f);
        dhol_slap(&b, t0 + 12 * step, 0.55f);
        if (fill) {
            for (int s = 8; s < 16; s++)
                dhol_slap(&b, t0 + s * step, s % 2 ? 0.28f : 0.42f);
            dhol_bass(&b, t0 + 14 * step, 0.7f);
        } else {
            const int ghost[] = { 2, 7, 9, 14, 15 };
            for (int k = 0; k < 5; k++)
                dhol_slap(&b, t0 + ghost[k] * step, 0.16f);
        }
        if (bi % 2 == 0)
            nagara(&b, t0, 0.9f);
    }

    /* War horn calls, then the shehnai carries the raga. */
    horn(&b, 0, bar * 0.9f, sa, sa * 0.8f, 0.4f, 0.35f, 0.2f, 0.4f, 0.74f, 0.008f, 0.2f);
    horn(&b, 8 * bar, bar * 0.9f, sa, sa * 0.8f, 0.4f, 0.35f, 0.2f, 0.4f, 0.74f, 0.008f, 0.2f);
    const int order[6] = { 0, 1, 2, 3, 1, 3 };
    float prev = 0;
    for (int i = 0; i < 6; i++)
        prev = play_phrase(&b, order[i], (4 + i * 2) * bar, beat, sa * 2, 0.26f, 0.8f,
                           0.12f, prev);
    temple_bell(&b, 0, 0.12f, 587);
    return to_music(&b, MUSIC_BATTLE);
}

static Music make_menu_music(void)
{
    const float bpm = 70, beat = 60 / bpm, bar = beat * 4;
    const int   bars = 8;
    const float len = bar * bars, sa = 146.83f;
    Buf b = buf_new(len, true);

    drone(&b, len, sa, 0.06f);
    tanpura(&b, len, bar / 2, sa, 0.26f);
    for (int bi = 0; bi < bars; bi++) {
        nagara(&b, bi * bar, 0.55f);
        dhol_slap(&b, bi * bar + beat * 3, 0.14f);
        dhol_bass(&b, bi * bar + beat * 2.5f, 0.25f);
    }
    /* A slow, breathy bansuri-like line. */
    float prev = 0;
    const int order[3] = { 0, 2, 3 };
    for (int i = 0; i < 3; i++)
        prev = play_phrase(&b, order[i], (2 + i * 2) * bar, beat, sa * 2, 0.2f, 0.45f,
                           0.3f, prev);
    temple_bell(&b, 0, 0.3f, 587);
    temple_bell(&b, 4 * bar, 0.18f, 440);
    return to_music(&b, MUSIC_MENU);
}

/* ------------------------------------------------------------------------ */
/* Sound pack loading, playback, mixing                                     */
/* ------------------------------------------------------------------------ */

static const char *SFX_FILE[SFX_COUNT] = {
    "step", "enemy_step", "blocked", "clash", "clash_heavy", "drum_roll", "conch",
    "victory", "defeat", "capture", "enemy_gate", "ui_move", "ui_select", "dice", "danger",
};
static const float SFX_VOLUME[SFX_COUNT] = {
    0.45f, 0.6f, 0.5f, 0.8f, 1.0f, 0.85f, 0.9f, 1.0f, 1.0f, 0.8f, 0.6f, 0.5f, 0.6f, 0.7f, 0.7f,
};
static const char *MUSIC_FILE[MUSIC_COUNT] = { "menu", "battle" };
#define MUSIC_VOLUME 0.55f

static Sound sounds[SFX_COUNT];
static Music music[MUSIC_COUNT];
static float music_vol[MUSIC_COUNT], music_target[MUSIC_COUNT];
static bool  ready, muted;
static int   pack_files;

/* Look for assets/<dir>/<name>.<ext> next to the executable or in the cwd. */
static const char *find_asset(const char *dir, const char *name)
{
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    (void)dir, (void)name; /* phones and browsers use the built-in sounds */
#else
    static const char *exts[] = { ".wav", ".ogg", ".mp3", ".flac" };
    const char *bases[] = { GetApplicationDirectory(), "" };
    for (int b = 0; b < 2; b++)
        for (int e = 0; e < 4; e++) {
            const char *path = TextFormat("%sassets/%s/%s%s", bases[b], dir, name, exts[e]);
            if (FileExists(path))
                return path;
        }
#endif
    return NULL;
}

void sfx_init(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        printf("Audio: no output device found, playing silently\n");
        return;
    }
    double start = GetTime();
    sine_init();

    for (int i = 0; i < SFX_COUNT; i++) {
        const char *path = find_asset("sfx", SFX_FILE[i]);
        if (path) {
            sounds[i] = LoadSound(path);
            pack_files++;
        } else if (i == SFX_CONCH) {
            Wave w = LoadWaveFromMemory(".mp3", CONCH_MP3, (int)CONCH_MP3_SIZE);
            /* If the recording can't be decoded, fall back to the synthesised conch. */
            sounds[i] = w.frameCount > 0 ? LoadSoundFromWave(w) : make_sfx(SFX_CONCH);
            printf("Audio: conch recording %.1fs\n", w.sampleRate ? (float)w.frameCount / w.sampleRate : 0.0f);
            UnloadWave(w);
        } else {
            sounds[i] = make_sfx((SfxId)i);
        }
    }
    for (int i = 0; i < MUSIC_COUNT; i++) {
        const char *path = find_asset("music", MUSIC_FILE[i]);
        if (path) {
            music[i] = LoadMusicStream(path);
            pack_files++;
        } else {
            music[i] = i == MUSIC_MENU ? make_menu_music() : make_battle_music();
        }
        music[i].looping = true;
    }
    ready = true;
    printf("Audio: ready (%d sound-pack files, synthesis took %.2fs)\n", pack_files,
           GetTime() - start);
    fflush(stdout);
}

void sfx_close(void)
{
    if (ready) {
        for (int i = 0; i < SFX_COUNT; i++)
            UnloadSound(sounds[i]);
        for (int i = 0; i < MUSIC_COUNT; i++) {
            UnloadMusicStream(music[i]);
            free(music_wav[i]);
        }
    }
    ready = false;
    if (IsAudioDeviceReady())
        CloseAudioDevice();
}

void sfx_play_ex(SfxId id, float volume, float pitch, float pan)
{
    if (!ready)
        return;
    SetSoundVolume(sounds[id], SFX_VOLUME[id] * volume);
    SetSoundPitch(sounds[id], pitch);
    SetSoundPan(sounds[id], pan);
    PlaySound(sounds[id]);
}

void sfx_play(SfxId id)
{
    sfx_play_ex(id, 1.0f, 1.0f, 0.0f);
}

void sfx_music(MusicId id)
{
    if (!ready)
        return;
    for (int i = 0; i < MUSIC_COUNT; i++) {
        music_target[i] = (i == (int)id) ? 1.0f : 0.0f;
        if (i == (int)id && !IsMusicStreamPlaying(music[i])) {
            music_vol[i] = 0.0f;
            SetMusicVolume(music[i], 0.0f);
            PlayMusicStream(music[i]);
        }
    }
}

void sfx_intensity(float level)
{
    if (ready)
        SetMusicPitch(music[MUSIC_BATTLE], 1.0f + 0.08f * fminf(1.0f, fmaxf(0.0f, level)));
}

void sfx_update(float dt)
{
    if (!ready)
        return;
    for (int i = 0; i < MUSIC_COUNT; i++) {
        /* Crossfade over about one second. */
        float step = dt * 1.2f;
        if (music_vol[i] < music_target[i])
            music_vol[i] = fminf(music_target[i], music_vol[i] + step);
        else
            music_vol[i] = fmaxf(music_target[i], music_vol[i] - step);

        if (IsMusicStreamPlaying(music[i])) {
            if (music_vol[i] <= 0.0f && music_target[i] <= 0.0f) {
                StopMusicStream(music[i]);
                continue;
            }
            SetMusicVolume(music[i], music_vol[i] * MUSIC_VOLUME);
            UpdateMusicStream(music[i]);
        }
    }
}

void sfx_toggle_mute(void)
{
    muted = !muted;
    SetMasterVolume(muted ? 0.0f : 1.0f);
}

bool sfx_muted(void) { return muted; }
int  sfx_pack_files(void) { return pack_files; }

/*
 * Render the gate through the M4L SHELL'S transport and write s16le stereo.
 *
 * WHAT THIS ADDS OVER tests/render_plugin.c, which renders the same four
 * seconds of the same patch and hashes to the same number.
 *
 * Sound parity between the shells is BY CONSTRUCTION: tg.gate~ links the same
 * libtg_capi.a the VST3 does, so there is no second implementation of the
 * envelope to drift. Re-rendering the engine would prove only that a static
 * library is deterministic.
 *
 * THE TRANSPORT IS NOT BY CONSTRUCTION. Every other shell is handed a beat
 * position: iPlug2 has GetPPQPos, the Move module gets one from the host
 * callbacks. Max is the only one that hands over TICKS, and the divisor
 * between them is a number this repository writes down exactly once --
 * TG_TICKS_PER_BEAT, in the M4L shell's wire.h. Get it wrong and the gate
 * still gates, in time with itself, at a tempo that is some rational multiple
 * of the host's. That reads as "the pattern drifts against the grid", which
 * is a bug report with no location in it.
 *
 * So this drives tg_m4l_transport from a TICK COUNTER -- the same call
 * tg_gate_perform64 makes, with the same arithmetic in front of it -- and
 * asserts the hash the beats-based path produces. A wrong divisor moves it.
 *
 * It also walks tg_m4l_param_value over all twelve, which is the other place
 * this shell can be silently wrong: three of them are one-based to a user,
 * three are percentages the engine wants as 0..1, and three MORE are
 * percentages the engine wants as percentages. That asymmetry is real and a
 * comment cannot enforce it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "trance_gate_core.h"
#include "wire.h"

#define SR    44100.0
#define BLOCK 128

/*
 * THE SAME NUMBER tests/render_plugin.c PINS, and it must stay the same
 * number. The comment there explains at length what has moved it in the past
 * (a per-step level latch, the Width-relative stage units, clang's FMA
 * contraction leaving with the C engine) and why each move was a real change
 * rather than a regression.
 *
 * If only THIS hash moves, nothing about the sound changed -- the M4L shell's
 * transport did, and it is the only thing here that could have.
 */
#define GOLDEN_FNV1A 0x13190E03DAB19715ULL

static uint64_t fnv = 0xcbf29ce484222325ULL;
static void fnv_add(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { fnv ^= b[i]; fnv *= 0x100000001b3ULL; }
}

static int fail = 0;
static void check(int ok, const char *what) {
    if (!ok) { printf("  FAIL: %s\n", what); fail = 1; }
}

/*
 * The twelve, in tg_param_t order, as a patcher sends them and as the engine
 * must receive them. Written out rather than computed: a table that derived
 * the expectation from the same switch it is testing would agree with any
 * bug in it.
 */
static void test_param_values(void)
{
    /* One-based to a user, zero-based to the engine. */
    check(tg_m4l_param_value(TG_P_SLOT,   1.0)  ==  0.0, "Slot 1 -> 0");
    check(tg_m4l_param_value(TG_P_SLOT,   8.0)  ==  7.0, "Slot 8 -> 7");
    check(tg_m4l_param_value(TG_P_LENGTH, 16.0) == 15.0, "Length 16 -> 15");
    check(tg_m4l_param_value(TG_P_LENGTH,  1.0) ==  0.0, "Length 1 -> 0");

    /* A percentage to a user, 0..1 to the engine. */
    check(tg_m4l_param_value(TG_P_AMOUNT,  100.0) == 1.0, "Amount 100% -> 1");
    check(tg_m4l_param_value(TG_P_HOLD,     75.0) == 0.75, "Width 75% -> 0.75");
    check(tg_m4l_param_value(TG_P_SUSTAIN,  60.0) == 0.6, "Sustain 60% -> 0.6");

    /*
     * A PERCENTAGE TO BOTH -- the asymmetry. These are a proportion of the
     * gate's Width, not of anything normalised, and scaling them here would
     * make every envelope a hundred times too fast.
     */
    check(tg_m4l_param_value(TG_P_ATTACK,  3.8267) == 3.8267, "Attack passes through");
    check(tg_m4l_param_value(TG_P_DECAY,  43.7333) == 43.7333, "Decay passes through");
    check(tg_m4l_param_value(TG_P_RELEASE, 27.3333) == 27.3333, "Release passes through");

    /* Indices and flags, untouched. */
    check(tg_m4l_param_value(TG_P_RATE,       7.0) == 7.0, "Rate passes through");
    check(tg_m4l_param_value(TG_P_LEGATO,     1.0) == 1.0, "Legato passes through");
    check(tg_m4l_param_value(TG_P_TIME_MODE,  1.0) == 1.0, "Time mode passes through");
    check(tg_m4l_param_value(TG_P_CURVE,      2.0) == 2.0, "Curve passes through");
}

static void test_transport(void)
{
    tg_transport_t t;

    /* 480 ticks is one beat. The whole point of the file. */
    t = tg_m4l_transport(1, 480.0, 120.0);
    check(t.running == 1 && t.beats == 1.0, "480 ticks is beat 1");
    t = tg_m4l_transport(1, 1920.0, 120.0);
    check(t.beats == 4.0, "1920 ticks is beat 4");

    /* A stopped transport is NO beat, not beat 0 -- otherwise the gate
     * resumes mid-pattern instead of from the top. */
    t = tg_m4l_transport(0, 9999.0, 120.0);
    check(t.running == 0 && t.beats == TG_NO_BEAT, "stopped is no beat");

    /* Live reports a negative position during count-in. */
    t = tg_m4l_transport(1, -480.0, 120.0);
    check(t.running == 0 && t.beats == TG_NO_BEAT, "count-in is no beat");

    /* A zero tempo would put an infinity into the engine's phase. */
    t = tg_m4l_transport(1, 0.0, 0.0);
    check(t.bpm == 120.0f, "a zero tempo falls back to 120");
}

int main(int argc, char **argv) {
    int verify = (argc > 1 && strcmp(argv[1], "--verify") == 0);
    double seconds = (argc > 1 && !verify) ? atof(argv[1]) : 4.0;

    tg_core_t *c = tg_core_create(SR);

    /* The identical patch tests/render_plugin.c and the module's own
     * render_ref.c set. Not a similar one -- the same one, or the hash below
     * is comparing two different pieces of music. */
    tg_core_set_param(c, "rate",    "1/16");
    tg_core_set_param(c, "length",  "15");
    tg_core_set_param(c, "pattern", "BEEF");
    tg_core_set_param(c, "ties",    "0022");
    tg_core_set_param(c, "attack",  "3.8267");
    tg_core_set_param(c, "decay",   "43.7333");
    tg_core_set_param(c, "sustain", "0.6");
    tg_core_set_param(c, "release", "27.3333");
    tg_core_set_param(c, "hold",    "0.75");
    tg_core_set_param(c, "amount",  "0.9");
    for (int s = 0; s < 16; s++) {
        char v[16];
        snprintf(v, sizeof(v), "%d", s);
        tg_core_set_param(c, "cursor", v);
        snprintf(v, sizeof(v), "%.3f", 0.35 + 0.04 * s);
        tg_core_set_param(c, "step_amount", v);
    }
    tg_core_set_param(c, "cursor", "0");

    const int total = (int)(seconds * SR);
    float L[BLOCK], R[BLOCK];
    int16_t out[BLOCK * 2];
    double phase = 0.0;
    const double w = 2.0 * M_PI * 220.0 / SR;

    /*
     * THE TRANSPORT ARRIVES AS TICKS, which is the one thing this renderer
     * does differently from tests/render_plugin.c. 123 BPM at 44100, so a
     * block of 128 frames is 128/44100 * 123/60 beats, and Max reports that
     * many times 480 ticks.
     *
     * THE 480 BELOW IS A LITERAL AND MUST STAY ONE. Writing
     * TG_TICKS_PER_BEAT here instead would make this renderer multiply by
     * exactly what tg_m4l_transport divides by, and the error would cancel:
     * the hash passed unchanged with the constant set to 960, proving only
     * that a number equals itself. The literal is Max's documented
     * resolution, stated INDEPENDENTLY of the code under test, which is the
     * only arrangement in which this hash means anything.
     */
    const double bpm = 123.0;
    const double max_ticks_per_beat = 480.0;
    double ticks = 0.0;

    for (int done = 0; done < total; done += BLOCK) {
        int n = (total - done) < BLOCK ? (total - done) : BLOCK;

        tg_transport_t t = tg_m4l_transport(1, ticks, bpm);

        for (int i = 0; i < n; i++) {
            /* Quantised to int16 BEFORE gating, because that is the buffer
             * the Move hands its renderer, and matching it exactly is what
             * makes these hashes comparable at all. */
            short q = (short)lrint(22000.0 * sin(phase));
            phase += w;
            L[i] = R[i] = (float)q;
        }
        tg_core_process_f32_split(c, L, R, n, &t);
        for (int i = 0; i < n; i++) {
            float l = L[i], r = R[i];
            out[i * 2]     = (int16_t)(l > 32767.0f ? 32767.0f : (l < -32768.0f ? -32768.0f : l));
            out[i * 2 + 1] = (int16_t)(r > 32767.0f ? 32767.0f : (r < -32768.0f ? -32768.0f : r));
        }
        if (verify) fnv_add(out, (size_t)n * 2 * sizeof(int16_t));
        else        fwrite(out, sizeof(int16_t), (size_t)n * 2, stdout);

        ticks += (n / SR) * (bpm / 60.0) * max_ticks_per_beat;
    }
    tg_core_destroy(c);

    if (!verify) return 0;

    test_param_values();
    test_transport();

    if (fnv != GOLDEN_FNV1A) {
        printf("  RENDER CHANGED: got 0x%016llX want 0x%016llX\n",
               (unsigned long long)fnv, (unsigned long long)GOLDEN_FNV1A);
        printf("  Only the M4L transport is new here -- if render_plugin still\n"
               "  passes, the ticks-to-beats conversion is what moved.\n");
        fail = 1;
    }
    if (fail) return 1;

    printf("  4s through the M4L transport matches the plugin render   ok\n");
    printf("  the twelve convert to the engine's units                 ok\n");
    return 0;
}

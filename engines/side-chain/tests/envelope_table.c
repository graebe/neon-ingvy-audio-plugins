// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Generates the ENVELOPE fixture -- and it MEASURES rather than transcribes.
 *
 * The trick, which is the Trance Gate's and worth stating again: drive the real
 * DSP with a DC input of 1.0 at depth 1.0, and the output sample IS the gain,
 * so it is `1 - duck` read straight off the thing that makes the sound. No
 * model of the envelope is consulted anywhere in producing this file.
 *
 * That is what caught `att_from` in the Trance Gate: five of sixty measured
 * cases started part way up the scale while the model insisted they started at
 * zero, and every one of them was a retrigger.
 *
 * Regenerate with
 *
 *     sc_envelope_table > engines/side-chain/tests/fixtures/envelope_table.txt
 *
 * Format: a "# case" line naming the settings, then one value per sample.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sc_core.h"

#define SR 48000.0
/* 120 bpm, 1/4 -> 24 000 samples per cycle. Decimated by 64 so a case is 375
 * rows rather than 24 000: the shape is what is being pinned, and every 64th
 * sample of a stage hundreds of samples long describes it completely. */
#define DECIMATE 64
#define FRAMES   (24000 + 4096)

/*
 * TWO MODES; the second is what ctest runs.
 *
 *   sc_envelope_table                    print the fixture
 *   sc_envelope_table --verify <file>    re-render and compare
 *
 * It RE-RENDERS rather than re-reading: the whole value of this fixture is that
 * it was measured off the real DSP, so verifying it has to measure again.
 *
 * TOL is 1e-6 and not 0. The fixture carries %.9g, the shortest form that
 * round-trips a float, so the comparison is against a value that has been
 * through decimal -- exact equality would assert something about printf rather
 * than about the envelope.
 */
#define VERIFY_TOL 1e-6

/* Render one case and return the decimated gains, or the count expected. */
static int render_case(int curve, double delay, double attack, double hold,
                       double release, int retrigger_at, float *out, int max)
{
    sc_core_t *c = sc_core_create(SR);
    sc_core_set_num(c, SC_P_SOURCE, 1);
    sc_core_set_num(c, SC_P_DEPTH, 1.0);
    sc_core_set_num(c, SC_P_CURVE, curve);
    sc_core_set_num(c, SC_P_DELAY, delay);
    sc_core_set_num(c, SC_P_ATTACK, attack);
    sc_core_set_num(c, SC_P_HOLD, hold);
    sc_core_set_num(c, SC_P_RELEASE, release);

    const unsigned char on[3] = { 0x90, 36, 127 };
    static float l[FRAMES], r[FRAMES];
    for (int i = 0; i < FRAMES; i++) { l[i] = 1.0f; r[i] = 1.0f; }
    sc_core_on_midi(c, on, 3, 0);
    if (retrigger_at > 0 && retrigger_at < FRAMES)
        sc_core_on_midi(c, on, 3, retrigger_at);
    sc_core_process_f32_split(c, l, r, FRAMES, NULL);

    int n = 0;
    for (int i = 0; i < FRAMES && n < max; i += DECIMATE)
        out[n++] = l[i];
    sc_core_destroy(c);
    return n;
}

static int verify(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 2; }

    static float got[FRAMES / DECIMATE + 8];
    int have = 0, idx = 0, cases = 0, rows = 0, bad = 0;
    char line[512];

    while (fgets(line, sizeof line, f)) {
        int curve, retrig;
        double d, a, h, rel;
        char name[64];
        if (sscanf(line, "# case %63s curve=%d delay=%lf attack=%lf hold=%lf "
                   "release=%lf retrigger=%d",
                   name, &curve, &d, &a, &h, &rel, &retrig) == 7) {
            have = render_case(curve, d, a, h, rel, retrig, got,
                               (int)(sizeof got / sizeof got[0]));
            idx = 0;
            cases++;
            continue;
        }
        if (line[0] == '#' || line[0] == '\n') continue;

        int sample; double want;
        if (sscanf(line, "%d %lf", &sample, &want) != 2) continue;
        if (idx >= have) continue;
        const double diff = fabs((double)got[idx] - want);
        if (diff > VERIFY_TOL) {
            if (bad < 10)
                fprintf(stderr, "case %d sample %d: %.9g vs %.9g\n",
                        cases, sample, (double)got[idx], want);
            bad++;
        }
        idx++;
        rows++;
    }
    fclose(f);

    if (cases < 27 || rows < 6000) {
        fprintf(stderr, "only %d cases / %d rows -- the fixture looks truncated\n",
                cases, rows);
        return 1;
    }
    printf("%s: %d cases, %d rows, %d mismatched\n", path, cases, rows, bad);
    return bad ? 1 : 0;
}

static void one_case(const char *name, int curve,
                     double delay, double attack, double hold, double release,
                     int retrigger_at)
{
    sc_core_t *c = sc_core_create(SR);
    sc_core_set_num(c, SC_P_SOURCE, 1);        /* MIDI: fires on demand   */
    sc_core_set_num(c, SC_P_DEPTH, 1.0);       /* so output == 1 - duck   */
    sc_core_set_num(c, SC_P_CURVE, curve);
    sc_core_set_num(c, SC_P_DELAY, delay);
    sc_core_set_num(c, SC_P_ATTACK, attack);
    sc_core_set_num(c, SC_P_HOLD, hold);
    sc_core_set_num(c, SC_P_RELEASE, release);

    printf("# case %s curve=%d delay=%g attack=%g hold=%g release=%g retrigger=%d\n",
           name, curve, delay, attack, hold, release, retrigger_at);

    const unsigned char on[3] = { 0x90, 36, 127 };
    /* One block, so the whole envelope is rendered against one tempo and the
     * sample offsets are unambiguous. */
    static float l[FRAMES], r[FRAMES];
    for (int i = 0; i < FRAMES; i++) { l[i] = 1.0f; r[i] = 1.0f; }

    sc_core_on_midi(c, on, 3, 0);
    if (retrigger_at > 0 && retrigger_at < FRAMES)
        sc_core_on_midi(c, on, 3, retrigger_at);

    /* No transport: the MIDI source does not need one, and leaving it out
     * keeps the cycle's phase-locked loop out of the measurement. */
    sc_core_process_f32_split(c, l, r, FRAMES, NULL);

    for (int i = 0; i < FRAMES; i += DECIMATE)
        printf("%d %.9g\n", i, (double)l[i]);

    sc_core_destroy(c);
}

static int verify(const char *path);

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--verify") == 0)
        return verify(argv[2]);

    printf("# sample gain -- the OUTPUT of a DC input at depth 1, i.e. 1 - duck\n");
    printf("# sample rate %g, 120 bpm, rate 1/4 (24000 samples per cycle)\n", SR);
    for (int curve = 0; curve < 3; curve++) {
        one_case("plain",        curve,  0,  5, 10,  40, 0);
        one_case("delayed",      curve, 20,  5, 10,  40, 0);
        one_case("slow-attack",  curve,  0, 60,  5,  30, 0);
        one_case("no-attack",    curve,  0,  0, 20,  50, 0);
        one_case("no-hold",      curve,  0, 10,  0,  60, 0);
        one_case("no-release",   curve,  0, 10, 20,   0, 0);
        one_case("long",         curve,  0, 25, 25, 150, 0);
        /* THE RETRIGGER CASES ARE THE POINT. A second trigger part way down
         * the release must anchor on the level the envelope is actually at. */
        one_case("retrig-rel",   curve,  0, 10, 10, 150, 9000);
        one_case("retrig-att",   curve,  0, 60, 10,  40, 600);
    }
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The envelope oracle: the gate's gain, measured from the real engine.
 *
 * WHY MEASURED AND NOT DERIVED.
 *
 * The web editor once drew the envelope with a second stage machine of its
 * own, in JavaScript, and that copy had drifted in two ways at once:
 * it waited for attack+decay to finish before closing the gate, where the
 * engine closes it at `frac >= hold` WHATEVER stage is running; and it
 * released from `sustain`, where the engine releases from the level it had
 * actually reached. Neither is visible unless you go looking with numbers.
 *
 * So this does not model anything. It runs the engine with
 *
 *     amount = 1, step depth = 1, input = DC 1.0
 *
 * where `m = 1 - amount * (1 - env * step_level)` collapses to `m = env`, and
 * the output sample IS the envelope. Whatever the engine does -- including
 * anything nobody has thought of -- lands in the table.
 *
 * SETTLED, NOT FIRST: the pattern is one step long and runs for several
 * cycles before the capture, so `att_from` carries over exactly as it does in
 * a host. A release that outlives its step is therefore in the table as the
 * engine really plays it, which is the case the plot most wanted to assume
 * away.
 *
 *   tg_envelope_table              writes it   (regenerate the fixture)
 *   tg_envelope_table --verify F   the ENGINE still agrees with F
 *   node --test envelope.test.mjs  the UI      still agrees with F
 */
#include "trance_gate_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * MEASURED AT 192k, WHICH IS NOT THE RATE ANYONE PLAYS AT.
 *
 * A stage's length is fixed in MILLISECONDS -- `stage_samples` is
 * `pct * 0.01 * width_ms * sr/1000` -- so the envelope's SHAPE does not depend
 * on the sample rate, only how finely it is resolved. The engine advances its
 * stage clock once per sample, so the reference is inherently quantised to a
 * sample; oversampling shrinks that to where it stops mattering, and lets the
 * comparison below be tight enough to be worth making.
 */
#define SR      192000.0
#define BPM     120.0f
#define SETTLE  8            /* cycles run before the one that is measured */
#define PTS     65           /* samples reported per step, t = i/64 */
/*
 * The engine runs its stage clock as `t += inc` per sample while this compares
 * against a level derived from the pattern PHASE, so the two can sit up to one
 * sample apart inside a stage. On the steepest case in the grid that is ~1.7e-3
 * of full scale at 192k. 3e-3 is comfortably inside that and still an order of
 * magnitude tighter than the faults it exists to catch, which were 0.3-0.5 off.
 */
#define TOL     3e-3

typedef struct { int curve; double a, d, s, r, hold; } Case;

/*
 * A CURATED GRID, NOT AN EXHAUSTIVE ONE. The full cross product is ~1700
 * cases and a 110k-line fixture nobody will ever read; these are chosen to
 * put a case on each edge the two implementations could disagree about.
 */
static int build_cases(Case *out, int max) {
    int n = 0;
    const double pct[] = { 0.0, 25.0, 100.0, 200.0 };
    const double sus[] = { 0.0, 0.5, 1.0 };

    for (int c = 0; c < 3 && n < max; c++) {
        /* The ordinary middle of the range, per curve and per sustain. */
        for (int si = 0; si < 3; si++)
            out[n++] = (Case){ c, 25.0, 25.0, sus[si], 25.0, 0.5 };

        /* ATTACK + DECAY LONGER THAN THE GATE. Stages go to 200% of width, so
         * this is reachable with the knobs and is where the old model held the
         * gate open past the close and then released from the wrong level. */
        out[n++] = (Case){ c, 100.0, 100.0, 0.5, 25.0, 0.5 };
        out[n++] = (Case){ c, 200.0,  50.0, 0.5, 25.0, 0.5 };  /* shuts mid-attack */
        out[n++] = (Case){ c,  50.0, 200.0, 0.3, 25.0, 0.5 };  /* shuts mid-decay  */
        out[n++] = (Case){ c, 200.0, 200.0, 0.5, 200.0, 0.9 };

        /* ZERO-LENGTH STAGES, which the engine walks THROUGH rather than
         * spending a sample in -- the whole reason Env::enter has a loop. */
        out[n++] = (Case){ c,   0.0,  25.0, 0.5, 25.0, 0.5 };
        out[n++] = (Case){ c,  25.0,   0.0, 0.5, 25.0, 0.5 };
        out[n++] = (Case){ c,  25.0,  25.0, 0.5,  0.0, 0.5 };
        out[n++] = (Case){ c,   0.0,   0.0, 0.5,  0.0, 0.5 };

        /* WIDTH 100%: the in-step release never runs (`if self.hold < 1.0`),
         * so the close happens at the step boundary instead. */
        out[n++] = (Case){ c,  25.0,  25.0, 0.5, 25.0, 1.0 };
        out[n++] = (Case){ c,   0.0,   0.0, 1.0,  0.0, 1.0 };

        /* A RELEASE THAT OUTLIVES ITS STEP, so the next attack starts from a
         * non-zero level. This is the case the plot draws as an isolated gate.
         */
        out[n++] = (Case){ c,  10.0,  10.0, 0.8, 200.0, 1.0 };

        /* Narrow and wide gates, and the extremes of sustain. */
        out[n++] = (Case){ c,  25.0,  25.0, 0.0, 25.0, 0.1 };
        out[n++] = (Case){ c,  25.0,  25.0, 1.0, 25.0, 0.1 };
        for (int pi = 0; pi < 4; pi++)
            out[n++] = (Case){ c, pct[pi], 30.0, 0.4, 30.0, 0.75 };
    }
    return n;
}

/*
 * ONE ISOLATED GATE: step 0 ON, steps 1..3 OFF, measured over step 0.
 *
 * FOUR STEPS AND NOT ONE, because the engine re-articulates only when the STEP
 * INDEX CHANGES -- `if Some(r.step) != self.last_step` in next_gain. A
 * one-step pattern therefore fires once at the very start and is silent for
 * ever after, which is a perfectly good engine behaviour and a useless
 * oracle: the first measurement looked like a dead gate.
 *
 * The three OFF steps after it give the release somewhere to run and leave
 * the envelope Idle before the next articulation, so the attack starts from
 * silence -- which is the isolated gate both plots draw. A run of ON steps
 * carries `att_from` across the boundary instead; that is a different picture
 * and not the one under test here.
 *
 * The step is the first QUARTER of the cycle, so t = phase01 * 4.
 */
#define STEPS_IN_CYCLE 4

static void measure(const Case *k, double *ts, double *vals) {
    tg_core_t *c = tg_core_create(SR);
    tg_core_set_param(c, "rate", "1/16");
    tg_core_set_num(c, TG_P_LENGTH, STEPS_IN_CYCLE - 1);   /* wire is an INDEX */
    tg_core_set_num(c, TG_P_LEGATO, 0);
    tg_core_set_num(c, TG_P_AMOUNT, 1.0);        /* m collapses to env */
    tg_core_set_num(c, TG_P_CURVE, k->curve);
    tg_core_set_num(c, TG_P_ATTACK, k->a);
    tg_core_set_num(c, TG_P_DECAY, k->d);
    tg_core_set_num(c, TG_P_SUSTAIN, k->s);
    tg_core_set_num(c, TG_P_RELEASE, k->r);
    tg_core_set_num(c, TG_P_HOLD, k->hold);

    /* The engine starts with a pattern already in it (5555), so the OFF steps
     * have to be set off rather than assumed. */
    for (int s = 0; s < STEPS_IN_CYCLE; s++) {
        char v[16];
        snprintf(v, sizeof v, "%d", s);
        tg_core_set_param(c, "cursor", v);
        tg_core_set_param(c, "step", s == 0 ? "1" : "0");
        if (s == 0) tg_core_set_param(c, "step_amount", "1.0");
    }

    tg_transport_t t = { 1, 0.0, BPM };
    double prev = -1.0;
    int wraps = 0;
    /* One cycle at 1/16, 120bpm, 48k is 24000 samples; SETTLE+1 of them. */
    const long limit = 4000000;
    static double phase[32768]; static double gain[32768]; int n = 0;

    for (long i = 0; i < limit; i++) {
        const double p = tg_core_phase01(c);
        if (p < prev) {                           /* the cycle wrapped */
            wraps++;
            if (wraps > SETTLE) break;            /* the measured cycle is done */
            n = 0;
        }
        prev = p;
        float l = 1.0f, r = 1.0f;
        tg_core_process_f32_split(c, &l, &r, 1, &t);
        t.beats += (1.0 / SR) * (BPM / 60.0);
        /* Step 0 only: the first 1/STEPS_IN_CYCLE of the cycle. */
        if (wraps >= SETTLE && p < 1.0 / STEPS_IN_CYCLE && n < 32768) {
            phase[n] = p * STEPS_IN_CYCLE;
            gain[n] = l;
            n++;
        }
    }
    tg_core_destroy(c);

    /*
     * The nearest recorded sample to each point of the reporting grid -- and
     * its ACTUAL phase is what goes in the table, not the grid position it
     * was chosen for. Handing the reader a tidy t it was never measured at
     * would put a whole sample of error into the comparison for nothing.
     */
    for (int i = 0; i < PTS; i++) {
        const double want = (double)i / (double)(PTS - 1);
        int best = -1; double bestd = 1e9;
        for (int j = 0; j < n; j++) {
            const double dd = fabs(phase[j] - want);
            if (dd < bestd) { bestd = dd; best = j; }
        }
        ts[i]   = best >= 0 ? phase[best] : want;
        vals[i] = best >= 0 ? gain[best] : 0.0;
    }
}

int main(int argc, char **argv) {
    const int verify = (argc > 2 && strcmp(argv[1], "--verify") == 0);
    if (argc > 1 && !verify) {
        fprintf(stderr, "usage: tg_envelope_table [--verify table.txt] > table.txt\n");
        return 2;
    }
    FILE *ref = NULL;
    if (verify && !(ref = fopen(argv[2], "r"))) {
        printf("  cannot open %s\n", argv[2]);
        return 1;
    }

    Case cases[256];
    const int ncases = build_cases(cases, 256);
    double vals[PTS], ts[PTS];
    int bad = 0;

    for (int ci = 0; ci < ncases; ci++) {
        const Case *k = &cases[ci];
        measure(k, ts, vals);
        for (int i = 0; i < PTS; i++) {
            const double t = ts[i];
            if (!verify) {
                printf("%d %d %.6g %.6g %.6g %.6g %.6g %.9g %.9g\n",
                       ci, k->curve, k->a, k->d, k->s, k->r, k->hold, t, vals[i]);
                continue;
            }
            int rci, rc; double ra, rd, rs, rr, rh, rt, rv;
            if (fscanf(ref, "%d %d %lf %lf %lf %lf %lf %lf %lf",
                       &rci, &rc, &ra, &rd, &rs, &rr, &rh, &rt, &rv) != 9) {
                printf("  TABLE SHORT at case %d point %d\n", ci, i);
                return 1;
            }
            if (rci != ci || rc != k->curve || fabs(rv - vals[i]) > TOL) {
                if (++bad <= 6)
                    printf("  case %d (curve %d a%.0f d%.0f s%.2f r%.0f w%.2f) "
                           "t %.4f: engine %.6f table %.6f\n",
                           ci, k->curve, k->a, k->d, k->s, k->r, k->hold,
                           t, vals[i], rv);
            }
        }
    }
    if (ref) fclose(ref);

    if (!verify) return 0;
    if (bad) {
        printf("  ENGINE ENVELOPE CHANGED: %d of %d points differ.\n",
               bad, ncases * PTS);
        printf("  If that was intended, regenerate the fixture.\n");
        return 1;
    }
    printf("  %d envelopes x %d points, measured from the engine, match    ok\n",
           ncases, PTS);
    return 0;
}

/*
 * The curve oracle: the engine's own shape(), tabulated.
 * Copyright (c) 2026 Torben Gräber. MIT.
 *
 * WHY A TABLE AND NOT AN EYE.
 *
 * The editor draws the envelope, so it needs shape() in JavaScript, and that
 * is a SECOND implementation of something the DSP already owns. The copy in
 * ui/src/lib/curves.js had the S-curve's first half un-mirrored for as long as
 * it existed: every picture of an S-curve in the plugin left the floor
 * vertically, and nothing caught it because a wrong curve is still a curve.
 *
 * So the two are pinned to one table, and the table is the engine's:
 *
 *   tg_shape_table              writes it   (regenerate the fixture)
 *   tg_shape_table --verify F   the ENGINE still agrees with F
 *   node --test curves.test.mjs the UI      still agrees with F
 *
 * Either side drifting now turns a test red. tg_test_shape and
 * tg_test_shape_inv exist in the C ABI for exactly this.
 *
 * The sweep is deliberately fine and deliberately includes both ends and the
 * S-curve's join at t = 0.5, which is where a mirrored half and an
 * un-mirrored one still agree and so the one point an eye cannot separate.
 */
#include "trance_gate_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STEPS 1000
#define NCURVES 3

/* Both use f64 exp/log, but not necessarily the SAME libm -- Rust's and
 * JavaScript's may land an ulp apart, and on a value near 1 that is ~1e-16.
 * Tight enough that a wrong formula cannot hide, loose enough that a
 * different libm is not a failure. */
#define TOL 1e-12

static int emit(int verify, FILE *ref)
{
    int bad = 0;
    for (int c = 0; c < NCURVES; c++)
    {
        for (int i = 0; i <= STEPS; i++)
        {
            const double t = (double)i / (double)STEPS;
            const double w = tg_test_shape(c, t);
            const double v = tg_test_shape_inv(c, t);

            if (!verify) { printf("%d %.17g %.17g %.17g\n", c, t, w, v); continue; }

            int rc; double rt, rw, rv;
            if (fscanf(ref, "%d %lf %lf %lf", &rc, &rt, &rw, &rv) != 4)
            {
                printf("  TABLE SHORT at curve %d t %.6f\n", c, t);
                return 1;
            }
            if (rc != c || fabs(rt - t) > TOL ||
                fabs(rw - w) > TOL || fabs(rv - v) > TOL)
            {
                if (++bad <= 5)
                    printf("  curve %d t %.6f: engine %.17g/%.17g table %.17g/%.17g\n",
                           c, t, w, v, rw, rv);
            }
        }
    }
    return bad;
}

int main(int argc, char **argv)
{
    const int verify = (argc > 2 && strcmp(argv[1], "--verify") == 0);
    if (argc > 1 && !verify)
    {
        fprintf(stderr, "usage: tg_shape_table [--verify table.txt] > table.txt\n");
        return 2;
    }

    FILE *ref = NULL;
    if (verify && !(ref = fopen(argv[2], "r")))
    {
        printf("  cannot open %s\n", argv[2]);
        return 1;
    }

    const int bad = emit(verify, ref);
    if (ref) fclose(ref);

    if (!verify) return 0;
    if (bad)
    {
        printf("  ENGINE CURVES CHANGED: %d of %d samples differ.\n",
               bad, NCURVES * (STEPS + 1));
        printf("  If that was intended, regenerate the fixture AND check\n");
        printf("  ui/src/lib/curves.js still matches it.\n");
        return 1;
    }
    printf("  3 curves x %d samples, forward and inverse, match the table    ok\n",
           STEPS + 1);
    return 0;
}

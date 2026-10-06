// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The fade oracle: the engine's own arrival weights, MEASURED.
 *
 * WHY A TABLE AND NOT A READING OF THE RUST.
 *
 * The editor has to draw what you are about to hear -- which pads have arrived,
 * how far the one in flight has got, and the same thing again on the ring and in
 * the pattern plot -- so ui/src/lib/fade.js is a SECOND implementation of
 * something the DSP owns. That is the arrangement curves.js is already in, and
 * the reason it is tolerable is this file: the two are pinned to one table, and
 * the table is the engine's.
 *
 *   tg_fade_table              writes it   (regenerate the fixture)
 *   tg_fade_table --verify F   the ENGINE still agrees with F
 *   node --test fade.test.mjs  the UI      still agrees with F
 *
 * MEASURED, LIKE THE ENVELOPE ORACLE AND FOR THE SAME REASON. The engine is run
 * with a DC input at amount 1 and no envelope at all -- attack, decay and
 * release at zero, sustain and Width at full -- so the gain during a step IS
 * that step's weight. Nothing here owes anything to anyone's reading of
 * recalc_fade; if the gain law and the weight table ever disagree, this reports
 * the gain law, which is the one a listener hears.
 *
 * THE SWEEP is every fade setting against four patterns, both shapes, and BOTH
 * DIRECTIONS -- In introduces the steps you drew on, Out introduces the holes,
 * and the weight this tabulates is the LEVEL FACTOR either way. It includes 0 and 1 because those are the two the control promises
 * outright, and 0.99999994 -- the float nearest 1 from beneath, which a host
 * can really send -- because that is where a missing epsilon leaves the last
 * arrival silent and nowhere else.
 */
#include "trance_gate_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* A measured gain against a computed weight. The engine quantises the step's
 * level to a byte on the way in (depth is 0..255), so full scale is exact and
 * anything else is within half a step of 1/255. The weights here are only ever
 * multiplied by a FULL depth, so this is generous by a wide margin and would
 * still catch a wrong formula. */
#define TOL 1e-4

#define SR    44100.0
#define BPM   120.0f
/* One 1/16 step at 120 BPM and 44100 Hz. Not an integer, which is the point of
 * sampling the MIDDLE of a step rather than counting boundaries. */
#define SPB   (SR * 60.0 / (double)BPM / 4.0)

static const char *PATTERNS[] = {
    "1111",      /* steps 0, 4, 8, 12 -- four arrivals, evenly spread  */
    "FFFF",      /* all sixteen -- the finest division the fade can do  */
    "0003",      /* steps 0 and 1 -- two arrivals, adjacent            */
    "8001",      /* steps 0 and 15 -- two, at the ends                 */
};
#define NPAT ((int)(sizeof(PATTERNS) / sizeof(PATTERNS[0])))

/* The fade settings, as the exact strings the engine is given -- so the fixture
 * and the JS test read the same numbers rather than two roundings of one. */
static const char *FADES[] = {
    "0", "0.0625", "0.125", "0.25", "0.3125", "0.375", "0.5",
    "0.625", "0.6875", "0.75", "0.875", "0.9375", "0.99", "0.99999994", "1",
};
#define NFADE ((int)(sizeof(FADES) / sizeof(FADES[0])))

/* A gate with no envelope, so a render reads back as weights. */
static tg_core_t *mk(const char *pattern, const char *fade, int soft, int out)
{
    tg_core_t *c = tg_core_create(SR);
    tg_core_set_param(c, "rate",      "1/16");
    tg_core_set_param(c, "length",    "15");    /* option index -> 16 steps */
    tg_core_set_param(c, "attack",    "0");
    tg_core_set_param(c, "decay",     "0");
    tg_core_set_param(c, "sustain",   "1");
    tg_core_set_param(c, "release",   "0");
    tg_core_set_param(c, "hold",      "1");
    tg_core_set_param(c, "amount",    "1");
    tg_core_set_param(c, "ties",      "0");
    tg_core_set_param(c, "pattern",   pattern);
    tg_core_set_param(c, "fade_soft", soft ? "1" : "0");
    tg_core_set_param(c, "fade_dir",  out ? "Out" : "In");
    tg_core_set_param(c, "fade",      fade);
    return c;
}

/* The gain at the centre of each of the 16 steps, from one render. */
static void weights(tg_core_t *c, double *out)
{
    const int frames = (int)(SPB * 16.0) + 64;
    float *l = (float *)malloc(sizeof(float) * (size_t)frames);
    float *r = (float *)malloc(sizeof(float) * (size_t)frames);
    tg_transport_t t;
    t.running = 1; t.bpm = BPM;
    for (int off = 0; off < frames; off += 64) {
        int n = (frames - off) < 64 ? (frames - off) : 64;
        for (int i = 0; i < n; i++) { l[off + i] = 1.0f; r[off + i] = 1.0f; }
        t.beats = (double)off / SR * ((double)BPM / 60.0);
        tg_core_process_f32_split(c, l + off, r + off, n, &t);
    }
    for (int i = 0; i < 16; i++) {
        int at = (int)(SPB * i + SPB * 0.5);
        out[i] = (at < frames) ? (double)l[at] : -1.0;
    }
    free(l); free(r);
}

/*
 * The drawn mask, out of the `ui` readout's first field. Needed since every step
 * carries a rank -- among its OWN KIND -- so the order can no longer say which
 * kind a step is, and the JS side has to be told the same thing the engine knows.
 */
static void mask(tg_core_t *c, int *out)
{
    char buf[4096];
    for (int i = 0; i < 16; i++) out[i] = 0;
    if (tg_core_get_param(c, "ui", buf, (int)sizeof buf) < 0) return;
    char *colon = strchr(buf, ':');
    if (!colon) return;
    *colon = '\0';
    const size_t len = strlen(buf);
    for (int i = 0; i < 16; i++) {
        const size_t nib = (size_t)(i / 4);
        if (nib >= len) break;
        char d[2] = { buf[len - 1 - nib], 0 };
        out[i] = ((int)strtol(d, NULL, 16) >> (i % 4)) & 1;
    }
}

/* The arrival ranks, out of the `ui` readout's ninth field. The JS side is
 * given these rather than deriving them, because deriving them is the engine's
 * job and this fixture is about the WEIGHTS. */
static void ranks(tg_core_t *c, int *out)
{
    char buf[4096];
    for (int i = 0; i < 16; i++) out[i] = 0;
    if (tg_core_get_param(c, "ui", buf, (int)sizeof buf) < 0) return;
    const char *p = buf;
    for (int i = 0; i < 8; i++) {
        p = strchr(p, ':');
        if (!p) return;
        p++;
    }
    for (int i = 0; i < 16; i++) {
        char pair[3] = { p[i * 2], p[i * 2 + 1], 0 };
        if (!pair[0] || !pair[1]) return;
        out[i] = (int)strtol(pair, NULL, 16);
    }
}

int main(int argc, char **argv)
{
    const char *verify = NULL;
    if (argc >= 3 && strcmp(argv[1], "--verify") == 0) verify = argv[2];

    FILE *f = NULL;
    if (verify) {
        f = fopen(verify, "r");
        if (!f) { fprintf(stderr, "fade_table: cannot open %s\n", verify); return 2; }
    } else {
        printf("# pattern out soft fade | on0..15 | rank0..15 | weight0..15\n");
        printf("# The engine's measured gain at the centre of each step, with no\n");
        printf("# envelope, so the gain IS the arrival weight. See fade_table.c.\n");
    }

    int bad = 0, rows = 0;
    for (int pi = 0; pi < NPAT; pi++)
    for (int out = 0; out < 2; out++)
    for (int soft = 0; soft < 2; soft++)
    for (int fi = 0; fi < NFADE; fi++) {
        tg_core_t *c = mk(PATTERNS[pi], FADES[fi], soft, out);
        double w[16]; int r[16], on[16];
        weights(c, w);
        ranks(c, r);
        mask(c, on);
        tg_core_destroy(c);

        char line[1024];
        int n = snprintf(line, sizeof line, "%s %d %d %s",
                         PATTERNS[pi], out, soft, FADES[fi]);
        for (int i = 0; i < 16; i++)
            n += snprintf(line + n, sizeof line - (size_t)n, " %d", on[i]);
        for (int i = 0; i < 16; i++)
            n += snprintf(line + n, sizeof line - (size_t)n, " %d", r[i]);
        for (int i = 0; i < 16; i++)
            n += snprintf(line + n, sizeof line - (size_t)n, " %.9g", w[i]);

        if (!verify) { printf("%s\n", line); rows++; continue; }

        /* Read the next non-comment line and compare the numbers, not the
         * text: %.9g is the shortest round-tripping form, but a reader that
         * compared strings would fail on a trailing zero. */
        char got[1024];
        do {
            if (!fgets(got, sizeof got, f)) {
                fprintf(stderr, "fade_table: fixture ended early at row %d\n", rows);
                fclose(f);
                return 1;
            }
        } while (got[0] == '#');
        char pat[32]; int gout, gsoft; char gfade[32];
        double gw[16]; int gr[16], gon[16];
        char *t = got;
        if (sscanf(t, "%31s %d %d %31s", pat, &gout, &gsoft, gfade) != 4) {
            fprintf(stderr, "fade_table: malformed fixture row %d\n", rows);
            fclose(f);
            return 1;
        }
        /* Walk past the four heads, then the 48 numbers. */
        for (int k = 0; k < 4; k++) { t = strchr(t, ' '); if (!t) break; t++; }
        for (int i = 0; i < 16 && t; i++) { gon[i] = atoi(t); t = strchr(t, ' '); if (t) t++; }
        for (int i = 0; i < 16 && t; i++) { gr[i] = atoi(t); t = strchr(t, ' '); if (t) t++; }
        for (int i = 0; i < 16 && t; i++) { gw[i] = atof(t); t = strchr(t, ' '); if (t) t++; }

        if (strcmp(pat, PATTERNS[pi]) || gout != out || gsoft != soft
            || strcmp(gfade, FADES[fi])) {
            fprintf(stderr, "fade_table: row %d is a different case (%s %d %d %s)\n",
                    rows, pat, gout, gsoft, gfade);
            bad++;
        }
        for (int i = 0; i < 16; i++)
            if (gon[i] != on[i]) {
                fprintf(stderr, "fade_table: %s step %d: on=%d, fixture %d\n",
                        PATTERNS[pi], i, on[i], gon[i]);
                bad++;
            }
        for (int i = 0; i < 16; i++) {
            if (gr[i] != r[i]) {
                fprintf(stderr, "fade_table: %s out=%d soft=%d fade=%s step %d: rank %d, fixture %d\n",
                        PATTERNS[pi], out, soft, FADES[fi], i, r[i], gr[i]);
                bad++;
            }
            if (fabs(gw[i] - w[i]) > TOL) {
                fprintf(stderr, "fade_table: %s out=%d soft=%d fade=%s step %d: %.9g, fixture %.9g\n",
                        PATTERNS[pi], out, soft, FADES[fi], i, w[i], gw[i]);
                bad++;
            }
        }
        rows++;
    }

    if (f) fclose(f);
    if (!verify) return 0;
    if (bad) { fprintf(stderr, "fade_table: %d disagreements over %d rows\n", bad, rows); return 1; }
    printf("fade_table: the engine agrees with the fixture over %d rows\n", rows);
    return 0;
}

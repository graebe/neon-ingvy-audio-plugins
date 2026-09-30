/*
 * Tests for the portable engine -- the three things the split made possible
 * and the shell could never exercise.
 *
 * The Schwung shell runs at 44100 with int16 interleaved buffers and reads
 * the transport from two host callbacks. None of that is true in a DAW, so
 * none of it was ever tested. These are the freedoms the core now has, and
 * each one is a way for the plugin to sound different from the hardware.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "trance_gate_core.h"

static int failures = 0;
static void check(const char *what, int ok) {
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}
static void check_near(const char *what, double got, double want, double tol) {
    int ok = fabs(got - want) <= tol;
    printf("  %-58s %s (got %.3f want %.3f)\n", what, ok ? "ok" : "FAIL", got, want);
    if (!ok) failures++;
}

/* One colon-separated field out of the `ui` readout, as a number. The layout
 * is steps:ties:length:phase:ms_step:advancing:cursor:depths. */
static double ui_field(tg_core_t *c, int index) {
    char buf[1024];
    if (tg_core_get_param(c, "ui", buf, sizeof(buf)) < 0) return -1.0;
    const char *p = buf;
    for (int i = 0; i < index; i++) {
        p = strchr(p, ':');
        if (!p) return -1.0;
        p++;
    }
    return atof(p);
}

/*
 * `frames` frames of DC through the SPLIT path, transport running, returned
 * as one float per frame. Callers free it.
 *
 * Split and not interleaved on purpose: with the interleaved path an index
 * into the buffer is half a frame, and a test that indexes it as if it were
 * mono reads the first half of its render twice over -- which is subtle,
 * plausible-looking, and quietly passed against the level-latching bug.
 */
static float *render_dc(tg_core_t *c, int frames, float bpm) {
    float *l = (float *)malloc(sizeof(float) * (size_t)frames);
    float *r = (float *)malloc(sizeof(float) * (size_t)frames);
    tg_transport_t t;
    t.running = 1; t.bpm = bpm;
    for (int off = 0; off < frames; off += 64) {
        int n = (frames - off) < 64 ? (frames - off) : 64;
        for (int i = 0; i < n; i++) { l[off + i] = 1.0f; r[off + i] = 1.0f; }
        t.beats = (double)off / 44100.0 * ((double)bpm / 60.0);
        tg_core_process_f32_split(c, l + off, r + off, n, &t);
    }
    free(r);
    return l;
}

/*
 * WHICH STEPS SOUND, MEASURED FROM THE AUDIO RATHER THAN ASKED.
 *
 * The fade's whole claim is about what you hear, so the tests below read it
 * out of a render instead of out of the readout -- a readout can agree with
 * itself while the gain law disagrees with both. DC in, no envelope, so the
 * gain during step i IS that step's weight and one sample in the middle of the
 * step is enough to read it.
 *
 * Returns the number of steps and fills `w` with the gain at the centre of
 * each. 44100 Hz, 120 BPM, 1/16 -- so a step is exactly 1378.125 samples and
 * the centre of step i is at 1378.125*i + 689.
 */
static int step_weights(tg_core_t *c, int length, float *w) {
    const double SPB = 44100.0 * 60.0 / 120.0 / 4.0;   /* samples per 1/16 */
    int frames = (int)(SPB * length) + 64;
    float *buf = render_dc(c, frames, 120.0f);
    for (int i = 0; i < length; i++) {
        int at = (int)(SPB * i + SPB * 0.5);
        w[i] = (at < frames) ? buf[at] : -1.0f;
    }
    free(buf);
    return length;
}

/* A gate with no envelope at all, so a render reads back as weights.
 * `length_index` is the OPTION index, as every length setter here takes it. */
static tg_core_t *mk_flat(int length_index, const char *pattern) {
    char li[16];
    snprintf(li, sizeof(li), "%d", length_index);
    tg_core_t *c = tg_core_create(44100.0);
    tg_core_set_param(c, "rate",    "1/16");
    tg_core_set_param(c, "length",  li);
    tg_core_set_param(c, "attack",  "0");
    tg_core_set_param(c, "decay",   "0");
    tg_core_set_param(c, "sustain", "1");
    tg_core_set_param(c, "release", "0");
    tg_core_set_param(c, "hold",    "1");
    tg_core_set_param(c, "amount",  "1");
    tg_core_set_param(c, "ties",    "0");
    tg_core_set_param(c, "pattern", pattern);
    return c;
}

/* One integer-valued key, read back through get_param. */
static int atoi_param(tg_core_t *c, const char *key) {
    char buf[64];
    if (tg_core_get_param(c, key, buf, sizeof(buf)) < 0) return -1;
    return atoi(buf);
}

/*
 * Is a step ON, from the `ui` readout's steps mask (field 0, LSB-aligned hex).
 *
 * Needed since the order field stopped being "0 when off": every step carries a
 * rank now -- among its own kind -- so the order can no longer stand in for the
 * mask the way it briefly could.
 */
static int ui_on(tg_core_t *c, int step) {
    char buf[4096];
    if (tg_core_get_param(c, "ui", buf, sizeof buf) < 0) return -1;
    char *colon = strchr(buf, ':');
    if (!colon) return -1;
    *colon = '\0';
    const size_t len = strlen(buf);
    const int nib = step / 4;                    /* which hex digit, from the END */
    if ((size_t) nib >= len) return 0;
    char d[2] = { buf[len - 1 - (size_t) nib], 0 };
    return ((int) strtol(d, NULL, 16) >> (step % 4)) & 1;
}

/* The order run out of the `ui` readout: field 8, two hex digits per step. */
static int ui_order(tg_core_t *c, int step) {
    char buf[4096];
    if (tg_core_get_param(c, "ui", buf, sizeof(buf)) < 0) return -1;
    const char *p = buf;
    for (int i = 0; i < 8; i++) {
        p = strchr(p, ':');
        if (!p) return -1;
        p++;
    }
    char pair[3] = { p[step * 2], p[step * 2 + 1], 0 };
    if (!pair[0] || !pair[1]) return -1;
    return (int)strtol(pair, NULL, 16);
}

/* A gate that is fully open on step 0 and fully shut on step 1, with no
 * envelope at all -- so the first sample that drops tells us exactly where the
 * step boundary fell, in samples. */
static tg_core_t *mk(double sr) {
    tg_core_t *c = tg_core_create(sr);
    tg_core_set_param(c, "rate",    "1/16");
    tg_core_set_param(c, "length",  "1");      /* index -> 2 steps */
    tg_core_set_param(c, "pattern", "1");      /* step 0 on, step 1 off */
    tg_core_set_param(c, "ties",    "0");
    tg_core_set_param(c, "attack",  "0");
    tg_core_set_param(c, "decay",   "0");
    tg_core_set_param(c, "sustain", "1");
    tg_core_set_param(c, "release", "0");
    tg_core_set_param(c, "hold",    "1");
    tg_core_set_param(c, "amount",  "1");
    return c;
}

/* How many frames of full-scale DC pass before the gate shuts. */
static int frames_until_gate_shuts(double sr, float bpm) {
    tg_core_t *c = mk(sr);
    tg_transport_t t = { 1, 0.0, bpm };
    const int BL = 64;
    float buf[64 * 2];
    int n = 0;
    for (int blk = 0; blk < 4000; blk++) {
        for (int i = 0; i < BL * 2; i++) buf[i] = 1.0f;
        tg_core_process_f32(c, buf, BL, &t);
        for (int i = 0; i < BL; i++) {
            if (buf[i * 2] < 0.5f) { tg_core_destroy(c); return n + i; }
        }
        n += BL;
        t.beats += (BL / sr) * (bpm / 60.0);
    }
    tg_core_destroy(c);
    return -1;
}

int main(void) {
    /*
     * SAMPLE RATE. A 1/16 step at 120 BPM is 0.125 s. The constant this
     * replaced was 44100, which at 48k makes every step 8.8% short and at 96k
     * makes it less than half as long -- the gate would simply run at the
     * wrong tempo, which is the one bug nobody would file as "a bug".
     */
    printf("sample rate:\n");
    struct { double sr; int want; } cases[] = {
        { 44100.0, 5512 }, { 48000.0, 6000 }, { 96000.0, 12000 },
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        char what[80];
        snprintf(what, sizeof(what), "%.0f Hz: one 1/16 step at 120 BPM", cases[i].sr);
        int got = frames_until_gate_shuts(cases[i].sr, 120.0f);
        /* +-1 frame: the boundary can land mid-sample. */
        check_near(what, got, cases[i].want, 1.0);
    }
    {
        tg_core_t *c = mk(48000.0);
        check("the rate is readable back", tg_core_get_sample_rate(c) == 48000.0);
        tg_core_set_sample_rate(c, 96000.0);
        check("and settable at runtime (prepareToPlay)", tg_core_get_sample_rate(c) == 96000.0);
        tg_core_set_sample_rate(c, 0.0);
        check("a nonsense rate is refused, not stored", tg_core_get_sample_rate(c) == 96000.0);
        tg_core_destroy(c);
    }

    /*
     * THE TWO BUFFER FORMATS MUST AGREE. If they do not, the plugin is not
     * the same effect as the hardware -- which is the entire premise.
     */
    printf("float and int16 agree:\n");
    {
        tg_core_t *a = mk(44100.0), *b = mk(44100.0);
        tg_transport_t t = { 1, 0.0, 123.0f };
        const int BL = 128;
        int16_t bi[128 * 2];
        float   bf[128 * 2];
        double worst = 0.0;
        for (int blk = 0; blk < 400; blk++) {
            for (int i = 0; i < BL; i++) {
                double v = 0.8 * sin(2.0 * M_PI * 220.0 * (blk * BL + i) / 44100.0);
                bi[i * 2] = bi[i * 2 + 1] = (int16_t)lrint(v * 32767.0);
                bf[i * 2] = bf[i * 2 + 1] = (float)v;
            }
            tg_core_process_i16(a, bi, BL, &t);
            tg_core_process_f32(b, bf, BL, &t);
            for (int i = 0; i < BL; i++) {
                double d = fabs(bi[i * 2] / 32767.0 - bf[i * 2]);
                if (d > worst) worst = d;
            }
            t.beats += (BL / 44100.0) * (123.0 / 60.0);
        }
        /* One int16 quantisation step is 1/32767 ~ 3.05e-5; allow two. */
        check_near("worst sample divergence is within quantisation", worst * 32767.0, 0.0, 2.0);
        tg_core_destroy(a); tg_core_destroy(b);
    }

    /* Interleaved and split must be the same maths, or VST3 and Move differ. */
    printf("interleaved and split agree:\n");
    {
        tg_core_t *a = mk(48000.0), *b = mk(48000.0);
        tg_transport_t t = { 1, 0.0, 120.0f };
        const int BL = 100;
        float inter[100 * 2], L[100], R[100];
        double worst = 0.0;
        for (int blk = 0; blk < 200; blk++) {
            for (int i = 0; i < BL; i++) {
                float v = (float)sin(0.01 * (blk * BL + i));
                inter[i * 2] = inter[i * 2 + 1] = v;
                L[i] = R[i] = v;
            }
            tg_core_process_f32(a, inter, BL, &t);
            tg_core_process_f32_split(b, L, R, BL, &t);
            for (int i = 0; i < BL; i++) {
                double d = fabs(inter[i * 2] - L[i]);
                if (d > worst) worst = d;
                d = fabs(inter[i * 2 + 1] - R[i]);
                if (d > worst) worst = d;
            }
            t.beats += (BL / 48000.0) * (120.0 / 60.0);
        }
        check("bit-identical across the two float layouts", worst == 0.0);
        tg_core_destroy(a); tg_core_destroy(b);
    }

    /*
     * A STOPPED TRANSPORT IS NOT BEAT 0. The shell used to detect this with a
     * negative beat position; the core is handed a flag. Getting it wrong
     * makes the gate re-trigger on every stop, and passes audio when it
     * should hold open.
     */
    printf("transport:\n");
    {
        tg_core_t *c = mk(44100.0);
        tg_transport_t stopped = { 0, 0.0, 120.0f };
        float buf[64 * 2];
        for (int i = 0; i < 64 * 2; i++) buf[i] = 1.0f;
        tg_core_process_f32(c, buf, 64, &stopped);
        check("stopped holds the gate open (passes dry)", buf[0] == 1.0f && buf[127] == 1.0f);
        tg_core_destroy(c);
    }

    /*
     * 128 STEPS. The masks were one uint32 and are now four, and the way that
     * breaks is silently: a step past 31 lands in the wrong word, or the top
     * word is never read, and the pattern simply misses beats nobody counts.
     */
    printf("128 steps:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        tg_core_set_param(c, "length", "127");        /* index -> 128 steps */
        char buf[64];
        tg_core_get_param(c, "length", buf, sizeof(buf));
        check("length reaches 128", atoi(buf) == 127);

        /* Set every step individually and read it back -- the only test that
         * catches a word-index bug at a boundary (31/32, 63/64, 95/96). */
        int bad = -1;
        for (int i = 0; i < 128; i++) {
            snprintf(buf, sizeof(buf), "%d", i);
            tg_core_set_param(c, "cursor", buf);
            tg_core_set_param(c, "step", (i % 3 == 0) ? "On" : "Off");
        }
        for (int i = 0; i < 128 && bad < 0; i++) {
            snprintf(buf, sizeof(buf), "%d", i);
            tg_core_set_param(c, "cursor", buf);
            tg_core_get_param(c, "step", buf, sizeof(buf));
            const char *want = (i % 3 == 0) ? "On" : "Off";
            if (strcmp(buf, want) != 0) bad = i;
        }
        check("every one of the 128 steps round-trips", bad < 0);
        if (bad >= 0) printf("      first wrong step: %d\n", bad);

        /* Step 127 specifically: the highest bit of the highest word. */
        tg_core_set_param(c, "cursor", "127");
        tg_core_set_param(c, "step", "Tie");
        tg_core_get_param(c, "step", buf, sizeof(buf));
        check("step 127 -- top bit of the top word -- holds a tie", strcmp(buf, "Tie") == 0);
        tg_core_destroy(c);
    }

    /* A <=32-step pattern must still emit the hex the previous version did,
     * or a patch stops moving between builds. */
    printf("hex compatibility:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        char buf[64];
        tg_core_set_param(c, "pattern", "5555");
        tg_core_get_param(c, "pattern", buf, sizeof(buf));
        check("a 16-step mask still reads back as \"5555\"", strcmp(buf, "5555") == 0);
        tg_core_set_param(c, "pattern", "FFFFFFFFFFFFFFFF");   /* 64 bits */
        tg_core_get_param(c, "pattern", buf, sizeof(buf));
        check("a 64-bit mask survives the round trip",
              strcmp(buf, "FFFFFFFFFFFFFFFF") == 0);
        tg_core_destroy(c);
    }

    /*
     * LEGATO. Below Sustain 100% an untied ON step re-articulates; legato
     * makes it hold. At 100% there is nothing to hear, which is why this
     * measures at 40%.
     */
    printf("legato:\n");
    {
        double lvl[2];
        for (int leg = 0; leg < 2; leg++) {
            tg_core_t *c = tg_core_create(44100.0);
            tg_core_set_param(c, "rate", "1/16");
            tg_core_set_param(c, "length", "1");       /* 2 steps, both ON */
            tg_core_set_param(c, "pattern", "3");
            tg_core_set_param(c, "ties", "0");
            tg_core_set_param(c, "attack", "30");
            tg_core_set_param(c, "decay", "1");
            tg_core_set_param(c, "sustain", "0.4");
            tg_core_set_param(c, "release", "0");
            tg_core_set_param(c, "hold", "1");
            tg_core_set_param(c, "amount", "1");
            tg_core_set_param(c, "legato", leg ? "1" : "0");

            tg_transport_t t = { 1, 0.0, 120.0f };
            const int BL = 64; float b[64 * 2];
            /* settle through step 0, then measure the first ms of step 1 */
            /* A 1/16 step at 120 BPM is 5512 samples = ~86 blocks of 64, so
             * step 1 begins at block 86. Measuring earlier than that measures
             * step 0 and reports no difference whatever legato does. */
            double acc = 0; int cnt = 0;
            for (int blk = 0; blk < 170; blk++) {
                for (int i = 0; i < BL * 2; i++) b[i] = 1.0f;
                tg_core_process_f32(c, b, BL, &t);
                if (blk >= 88 && blk < 150) { for (int i = 0; i < BL; i++) { acc += b[i*2]; cnt++; } }
                t.beats += (BL / 44100.0) * 2.0;
            }
            lvl[leg] = acc / cnt;
            tg_core_destroy(c);
        }
        printf("      step 1 level: legato off %.3f, on %.3f\n", lvl[0], lvl[1]);
        /* OFF retriggers: attack climbs 0.4 -> 1.0 each step, so the mean sits
         * ABOVE sustain. ON holds flat AT sustain. The sign matters -- getting
         * it backwards is how a switch that does nothing looks like it works. */
        check("legato OFF re-articulates (mean above sustain)", lvl[0] > 0.45);
        check("legato ON holds flat at sustain", lvl[1] < 0.45);
        check("and the two genuinely differ", lvl[0] - lvl[1] > 0.05);
    }

    /*
     * LEGATO AND WIDTH TOGETHER, WHICH NOTHING ELSE TESTS.
     *
     * "Join Neighbors" means every adjacent ON pair is tied, and a tie
     * overrides the gate length -- so legato has to override it too, or the
     * two controls contradict each other. When it did not, the result was not
     * a cosmetic difference: the gate released inside step 0, reached
     * TG_IDLE, and legato then suppressed the attack that would have
     * restarted it. TG_IDLE is absorbing, so the pattern went SILENT and
     * stayed silent for every step after the first.
     *
     * THIS BLOCK IS THE REASON THE FIX CAN BE RELIED ON. The legato test
     * above pins hold = 1, where the gate rule never runs; the gate-length
     * tests in test_gate.c use adjacent ON steps but leave legato off. The
     * bug lived in the one square neither covered, and it has already been
     * reintroduced once by a rewrite of the surrounding function.
     */
    printf("legato and width together:\n");
    {
        double mean[2], lastStep[2];
        for (int leg = 0; leg < 2; leg++) {
            tg_core_t *c = tg_core_create(48000.0);
            tg_core_set_param(c, "rate", "1/16");
            tg_core_set_param(c, "length", "7");       /* 8 steps */
            tg_core_set_param(c, "pattern", "ff");     /* all ON */
            tg_core_set_param(c, "ties", "0");
            /* Stages are PERCENTAGES OF WIDTH now; sustain 1 keeps the decay
             * out of the measurement either way. */
            tg_core_set_param(c, "attack", "2");
            tg_core_set_param(c, "decay", "10");
            tg_core_set_param(c, "sustain", "1");
            tg_core_set_param(c, "release", "10");
            tg_core_set_param(c, "hold", "0.5");
            tg_core_set_param(c, "amount", "1");
            tg_core_set_param(c, "legato", leg ? "1" : "0");

            /* 1/16 at 120 BPM = 125 ms = 6000 frames at 48k. */
            const int per = 6000, BL = 64;
            double acc = 0, accLast = 0; int cnt = 0, cntLast = 0;
            float b[64 * 2];
            for (int i = 0; i < per * 8; i += BL) {
                for (int k = 0; k < BL * 2; k++) b[k] = 1.0f;
                tg_transport_t t = { 1, (double)i / (double)per * 0.25, 120.0f };
                tg_core_process_f32(c, b, BL, &t);
                for (int k = 0; k < BL; k++) {
                    acc += b[k*2]; cnt++;
                    if (i + k >= per * 7) { accLast += b[k*2]; cntLast++; }
                }
            }
            mean[leg]     = acc / cnt;
            lastStep[leg] = accLast / cntLast;
            tg_core_destroy(c);
        }
        printf("      8 ON steps at width 50%%: legato off %.3f, on %.3f\n",
               mean[0], mean[1]);
        check("legato holds the gate open across the run", mean[1] > 0.95);
        check("...and legato OFF still closes it every step", mean[0] < 0.75);
        /* The regression that matters. Before the fix this read 0.000: the
         * gate shut after step 0 and never reopened, for this pattern or any
         * pattern after it. */
        check("...and the last step has not been silenced", lastStep[1] > 0.95);
    }
    {
        /* A next step that is OFF still closes the gate, which is what keeps
         * legato from collapsing into "tie everything". */
        tg_core_t *c = tg_core_create(48000.0);
        tg_core_set_param(c, "rate", "1/16");
        tg_core_set_param(c, "length", "1");       /* 2 steps */
        tg_core_set_param(c, "pattern", "1");      /* step 0 ON, step 1 OFF */
        tg_core_set_param(c, "ties", "0");
        tg_core_set_param(c, "attack", "0");  tg_core_set_param(c, "decay", "0");
        tg_core_set_param(c, "sustain", "1"); tg_core_set_param(c, "release", "0");
        tg_core_set_param(c, "hold", "0.5");
        tg_core_set_param(c, "amount", "1");
        tg_core_set_param(c, "legato", "1");

        const int per = 6000, BL = 64;
        double acc = 0; int cnt = 0;
        float b[64 * 2];
        for (int i = 0; i < per; i += BL) {
            for (int k = 0; k < BL * 2; k++) b[k] = 1.0f;
            tg_transport_t t = { 1, (double)i / (double)per * 0.25, 120.0f };
            tg_core_process_f32(c, b, BL, &t);
            for (int k = 0; k < BL; k++)
                if (i + k > per * 6 / 10) { acc += b[k*2]; cnt++; }
        }
        check("legato does not hold through into an OFF step", (acc / cnt) < 0.05);
        tg_core_destroy(c);
    }

    /*
     * STATE SIZE. The slot budget is 8192 and a bus insert's is 1024, and an
     * oversized blob is DROPPED rather than truncated -- so the number worth
     * knowing is where the bus case stops working, not whether it does.
     */
    /*
     * THE `params` READOUT. Fifteen automatable values plus width_ms in one
     * read, so a shell does not take nine locks to ask "did anything move".
     * Two properties matter and neither is obvious from looking at it: every
     * field must agree with the single-key getter for the same key, and the
     * floats must survive a round trip EXACTLY -- a reader that writes back
     * what it read must not move the patch.
     */
    printf("the params readout:\n");
    {
        tg_core_t *c = tg_core_create(48000.0);
        tg_core_set_param(c, "slot", "3");
        tg_core_set_param(c, "legato", "1");
        tg_core_set_param(c, "time_mode", "1");
        tg_core_set_param(c, "curve", "2");
        tg_core_set_param(c, "rate", "1/8");
        tg_core_set_param(c, "length", "31");      /* index -> 32 steps */
        /* Awkward on purpose: values a %.1f or %.2f getter would round. */
        tg_core_set_param(c, "amount",  "0.123456789");
        tg_core_set_param(c, "hold",    "0.987654321");
        tg_core_set_param(c, "attack",  "123.456789");
        tg_core_set_param(c, "decay",   "0.0123456789");
        tg_core_set_param(c, "sustain", "0.333333343");
        tg_core_set_param(c, "release", "199.999985");

        char line[TG_STATE_MAX];
        int n = tg_core_get_param(c, "params", line, sizeof(line));
        check("params answers at all", n > 0);

        /* Split on ':' -- 16 fields: the twelve automatable values, width_ms,
         * and the fade's three on the end. */
        char *f[24]; int nf = 0;
        for (char *t = line; nf < 24; ) {
            f[nf++] = t;
            char *colon = strchr(t, ':');
            if (!colon) break;
            *colon = '\0'; t = colon + 1;
        }
        check("params has sixteen fields", nf == 16);

        char one[TG_STATE_MAX];
        #define MIRRORS(idx, key) \
            (tg_core_get_param(c, key, one, sizeof(one)) >= 0 && strcmp(f[idx], one) == 0)
        check("...slot mirrors get_param",      MIRRORS(0, "slot"));
        check("...legato mirrors get_param",    MIRRORS(1, "legato"));
        check("...time_mode mirrors get_param", MIRRORS(2, "time_mode"));
        check("...curve mirrors get_param",     MIRRORS(3, "curve"));
        check("...rate is the LABEL, as get_param answers it", MIRRORS(4, "rate"));
        check("...length is the OPTION INDEX, as get_param answers it",
              MIRRORS(5, "length"));
        /* The fade's switch, appended past width_ms. Its knob is a float and is
         * covered by the round trip below. */
        check("...fade_soft mirrors get_param", MIRRORS(14, "fade_soft"));
        check("...fade_dir mirrors get_param",  MIRRORS(15, "fade_dir"));
        #undef MIRRORS

        /*
         * The round trip. Feeding each float back through set_param must land
         * on the same bits -- that is the whole reason for %.9g, and it is
         * what lets a caller read-modify-write without a suppression flag.
         */
        static const char *fkeys[] = { "amount", "hold", "attack",
                                       "decay", "sustain", "release" };
        int exact = 1;
        for (int i = 0; i < 6; i++) {
            tg_core_set_param(c, fkeys[i], f[6 + i]);
            char back[TG_STATE_MAX];
            tg_core_get_param(c, "params", back, sizeof(back));
            /* re-split and compare just this field */
            char *g[24]; int ng = 0;
            for (char *t = back; ng < 24; ) {
                g[ng++] = t;
                char *colon = strchr(t, ':');
                if (!colon) break;
                *colon = '\0'; t = colon + 1;
            }
            if (ng != 16 || strcmp(g[6 + i], f[6 + i]) != 0) {
                printf("      %s: wrote %s, read %s\n", fkeys[i], f[6 + i],
                       (ng == 16) ? g[6 + i] : "(short line)");
                exact = 0;
            }
        }
        check("every float round-trips bit-exactly", exact);

        tg_core_destroy(c);
    }
    {
        /*
         * width_ms is field 12 -- it WAS the last one, and the fade's three are
         * appended past it. Found by INDEX now rather than by the last colon,
         * because "the last field" stopped being a definition of it the moment
         * anything could follow: an strrchr here would have read fade_soft and
         * compared an integer flag against a duration.
         */
        tg_core_t *c = tg_core_create(48000.0);
        tg_core_set_param(c, "rate", "1/16");
        tg_core_set_param(c, "hold", "0.5");
        char line[TG_STATE_MAX], w[TG_STATE_MAX];
        tg_core_get_param(c, "params", line, sizeof(line));
        tg_core_get_param(c, "width_ms", w, sizeof(w));
        char *f[24]; int nf = 0;
        for (char *t = line; nf < 24; ) {
            f[nf++] = t;
            char *colon = strchr(t, ':');
            if (!colon) break;
            *colon = '\0'; t = colon + 1;
        }
        check("params carries width_ms at field 12",
              nf == 16 && fabs(atof(f[12]) - atof(w)) < 0.01);
        tg_core_destroy(c);
    }

    /*
     * THE TWO DOORS AGREE. tg_core_set_param parses a string and delegates to
     * tg_core_set_num, so every clamp exists once -- this is the assertion
     * that keeps it that way. Two instances driven the same way, one through
     * each door, must end up with byte-identical state, INCLUDING at the ends
     * where the clamps bite.
     */
    printf("numeric and string setters agree:\n");
    {
        struct { tg_param_t p; const char *key; double v; } sweep[] = {
            { TG_P_SLOT,      "slot",      3    }, { TG_P_SLOT,      "slot",      99   },
            { TG_P_LENGTH,    "length",    31   }, { TG_P_LENGTH,    "length",    999  },
            { TG_P_RATE,      "rate",      5    }, { TG_P_RATE,      "rate",      -4   },
            { TG_P_LEGATO,    "legato",    1    }, { TG_P_TIME_MODE, "time_mode", 1    },
            { TG_P_CURVE,     "curve",     2    }, { TG_P_CURVE,     "curve",     77   },
            { TG_P_AMOUNT,    "amount",    0.37 }, { TG_P_AMOUNT,    "amount",    9.0  },
            { TG_P_HOLD,      "hold",      0.62 }, { TG_P_HOLD,      "hold",      -1.0 },
            { TG_P_ATTACK,    "attack",    42.5 }, { TG_P_ATTACK,    "attack",    999  },
            { TG_P_DECAY,     "decay",     7.25 }, { TG_P_DECAY,     "decay",     -5   },
            { TG_P_SUSTAIN,   "sustain",   0.45 }, { TG_P_RELEASE,   "release",   88.0 },
        };
        int same = 1;
        for (unsigned i = 0; i < sizeof(sweep)/sizeof(sweep[0]); i++) {
            tg_core_t *a = tg_core_create(48000.0);
            tg_core_t *b = tg_core_create(48000.0);
            char num[64];
            /* %.17g so the STRING door is not the one losing precision --
             * this test is about the clamps, not about formatting. */
            snprintf(num, sizeof(num), "%.17g", sweep[i].v);

            tg_core_set_num(a, sweep[i].p, sweep[i].v);
            tg_core_set_param(b, sweep[i].key, num);

            char sa[TG_STATE_MAX], sb[TG_STATE_MAX];
            tg_core_get_param(a, "state", sa, sizeof(sa));
            tg_core_get_param(b, "state", sb, sizeof(sb));
            if (strcmp(sa, sb) != 0) {
                printf("      %s = %g diverged\n", sweep[i].key, sweep[i].v);
                same = 0;
            }
            tg_core_destroy(a); tg_core_destroy(b);
        }
        check("both doors land on identical state, clamps included", same);
    }

    /*
     * THE THREE CONSTANTS THE HEADER PUBLISHES ABOUT THE RATE LADDER.
     *
     * A plugin has to declare its host parameters before any audio runs, so
     * it needs the option count, the default index and the stage scale as
     * compile-time numbers -- it cannot ask get_param. Publishing them means
     * two copies of one fact, and this is the assertion that keeps them one:
     * every value is checked against what the engine DOES rather than against
     * what the header says.
     */
    printf("the header's rate ladder matches the engine:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        char seen[TG_NUM_RATES][32];
        int distinct = 1, all_answered = 1;
        for (int i = 0; i < TG_NUM_RATES; i++) {
            char v[16]; snprintf(v, sizeof(v), "%d", i);
            tg_core_set_param(c, "rate", v);
            if (tg_core_get_param(c, "rate", seen[i], sizeof(seen[i])) <= 0)
                all_answered = 0;
            for (int j = 0; j < i; j++)
                if (strcmp(seen[i], seen[j]) == 0) distinct = 0;
        }
        check("every index 0..TG_NUM_RATES-1 names a rate", all_answered);
        check("...and they are all different", distinct);

        /* One past the end must NOT name a fourteenth rate. Out of range is
         * the default, which is how the engine has answered since indices
         * were first accepted. */
        char past[32], deflt[32];
        char v[16]; snprintf(v, sizeof(v), "%d", TG_NUM_RATES);
        tg_core_set_param(c, "rate", v);
        tg_core_get_param(c, "rate", past, sizeof(past));
        snprintf(v, sizeof(v), "%d", TG_RATE_DEFAULT);
        tg_core_set_param(c, "rate", v);
        tg_core_get_param(c, "rate", deflt, sizeof(deflt));
        check("TG_NUM_RATES is the end of the table",
              strcmp(past, deflt) == 0);
        check("TG_RATE_DEFAULT is 1/16, as the table's comment says",
              strcmp(deflt, "1/16") == 0);

        /* The labels a plugin declares its Rate parameter from. */
        int labels_agree = 1;
        for (int i = 0; i < TG_NUM_RATES; i++) {
            char label[32];
            if (tg_core_rate_label(i, label, sizeof(label)) <= 0 ||
                strcmp(label, seen[i]) != 0)
                labels_agree = 0;
        }
        char label[32];
        check("tg_core_rate_label names the same rates get_param does", labels_agree);
        check("...and nothing past the end",
              tg_core_rate_label(TG_NUM_RATES, label, sizeof(label)) == -1 &&
              tg_core_rate_label(-1, label, sizeof(label)) == -1);
        check("...and refuses a buffer it does not fit",
              tg_core_rate_label(0, label, 2) == -1);
        check("tg_core_rate_default is TG_RATE_DEFAULT",
              tg_core_rate_default() == TG_RATE_DEFAULT);

        /* TG_STAGE_MAX_PCT is the clamp, so one past it must come back AT
         * it. */
        snprintf(v, sizeof(v), "%f", (double)TG_STAGE_MAX_PCT + 50.0);
        tg_core_set_param(c, "attack", v);
        char a[32]; tg_core_get_param(c, "attack", a, sizeof(a));
        printf("      %g%% asked for, %s%% given back\n",
               (double)TG_STAGE_MAX_PCT + 50.0, a);
        check("TG_STAGE_MAX_PCT is where a stage clamps",
              fabs(atof(a) - (double)TG_STAGE_MAX_PCT) < 0.05);
        tg_core_destroy(c);
    }

    /*
     * THE PATTERN PLOT'S CURVE AND THE SCOPE'S SWEEP, through the header a
     * plugin compiles against.
     */
    printf("the plot's curve and the scope's sweep:\n");
    {
        tg_core_t *c = tg_core_create(48000.0);
        char state[TG_STATE_MAX], gate[TG_GATE_MAX];
        tg_core_get_param(c, "state", state, sizeof(state));
        const int n = tg_core_render_gate(state, gate, sizeof(gate));
        check("a patch renders a curve", n > 0 && strncmp(gate, "16:64:", 6) == 0);
        check("...one raw byte per sample of a cycle", n == 6 + 16 * 64);
        check("...and a buffer too small is refused, not truncated",
              tg_core_render_gate(state, gate, 64) == -1);
        check("nothing to draw is -1", tg_core_render_gate("", gate, sizeof(gate)) == -1);

        float l[256], r[256], sweep[256];
        for (int i = 0; i < 256; i++) { l[i] = r[i] = 0.5f; sweep[i] = -1.f; }
        tg_transport_t t = { 1, 0.0, 120.0f };
        tg_core_process_f32_split_tap(c, l, r, sweep, 256, &t);
        int in_range = 1, rising = 1;
        for (int i = 0; i < 256; i++) {
            if (!(sweep[i] >= 0.f && sweep[i] < 1.f)) in_range = 0;
            if (i && sweep[i] < sweep[i - 1]) rising = 0;
        }
        check("the sweep is a cycle phase, 0..1", in_range);
        check("...and rises across a running block", rising);
        tg_core_process_f32_split_tap(c, l, r, NULL, 256, &t);
        check("a null sweep is allowed", 1);
        tg_core_destroy(c);
    }

    printf("state size:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        char buf[8192];
        int plain = 0, accented = 0;
        for (int s = 0; s < 8; s++) {
            char v[16]; snprintf(v, sizeof(v), "%d", s);
            tg_core_set_param(c, "slot", v);
            tg_core_set_param(c, "length", "127");
            tg_core_set_param(c, "pattern", "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
        }
        plain = tg_core_get_param(c, "state", buf, sizeof(buf));
        printf("      8 slots x 128 steps, no accents : %5d bytes\n", plain);
        for (int s = 0; s < 8; s++) {
            char v[16]; snprintf(v, sizeof(v), "%d", s);
            tg_core_set_param(c, "slot", v);
            for (int i = 0; i < 128; i++) {
                snprintf(v, sizeof(v), "%d", i);
                tg_core_set_param(c, "cursor", v);
                tg_core_set_param(c, "step_amount", "0.5");
            }
        }
        accented = tg_core_get_param(c, "state", buf, sizeof(buf));
        printf("      the same, every step accented   : %5d bytes\n", accented);
        check("worst case fits an audio FX slot (8192)", accented > 0 && accented <= 8192);
        check("no-accent case fits a bus insert (1024)", plain > 0 && plain <= 1024);
        /*
         * AND IT FITS THE NUMBER EMBEDDERS SIZE THEIR BUFFERS FROM.
         *
         * This was a _Static_assert in the C engine, computed from the format
         * macros; the Rust engine has no macros to compute it from, so it is
         * measured here instead -- against the real emitter rather than a
         * conservative bound over it, which is the stronger check of the two.
         * It is not decoration: a shell that sized this buffer at 2048 got a
         * SILENTLY TRUNCATED patch, because get_param has snprintf semantics
         * and a truncated blob reloads as the wrong pattern with nothing said.
         */
        check("worst case fits TG_STATE_MAX, which embedders size from",
              accented > 0 && accented < TG_STATE_MAX);
        if (accented > 1024)
            printf("      note: a fully accented 8x128 patch exceeds a BUS INSERT's\n"
                   "            1024 bytes. Slots are unaffected.\n");
        tg_core_destroy(c);
    }

    /*
     * THE STEP DURATION IS KNOWN BEFORE ANY AUDIO RUNS, and follows the rate
     * immediately. The `ui` readout is where a UI learns it, and the plugin
     * draws its envelope against it -- so a value that only appeared after
     * the first block meant a blank panel on open, and one that only updated
     * on a block meant the picture disagreed with the Rate knob while the
     * transport sat still.
     */
    printf("step duration in the ui readout:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);

        check_near("1/16 at 120 BPM reads 125 ms before any block",
                   ui_field(c, 4), 125.0, 0.5);

        tg_core_set_param(c, "rate", "1/32");
        check_near("...and follows the rate with no block between",
                   ui_field(c, 4), 62.5, 0.5);

        tg_core_set_param(c, "rate", "1/4");
        check_near("...at the slow end too", ui_field(c, 4), 500.0, 0.5);

        /* A block at a real tempo replaces the nominal 120. */
        float lr[128];
        for (int i = 0; i < 128; i++) lr[i] = 0.0f;
        tg_transport_t t;
        t.running = 1; t.beats = 0.0; t.bpm = 174.0f;
        tg_core_process_f32(c, lr, 64, &t);
        check_near("a block hands over the host's real tempo",
                   ui_field(c, 4), 60000.0 / 174.0, 1.0);

        tg_core_destroy(c);
    }

    /*
     * THE STRUCK STEP'S LEVEL OWNS THE WHOLE GATE.
     *
     * The level used to be read as depth[current_step] every sample, so a
     * release that outlived its step was scaled by the NEXT step's amount --
     * pulling a pad to 25% quietened the body of its envelope and left the
     * tail at whatever followed. Two steps at 100% and 25% and a release long
     * enough to cross the boundary is the smallest case that shows it.
     */
    printf("the level is latched at gate-open:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        tg_core_set_param(c, "rate",    "1/16");
        tg_core_set_param(c, "length",  "1");      /* index -> 2 steps */
        tg_core_set_param(c, "pattern", "1");      /* step 0 on, step 1 off */
        tg_core_set_param(c, "ties",    "0");
        tg_core_set_param(c, "attack",  "0");
        tg_core_set_param(c, "decay",   "0");
        tg_core_set_param(c, "sustain", "1");
        tg_core_set_param(c, "hold",    "1");
        tg_core_set_param(c, "amount",  "1");
        /* Step 0 struck at a QUARTER, step 1 (where the tail lands) at full. */
        tg_core_set_param(c, "cursor", "0");
        tg_core_set_param(c, "step_amount", "0.25");
        tg_core_set_param(c, "cursor", "1");
        tg_core_set_param(c, "step_amount", "1.00");
        /* One step is 125 ms at 120 BPM; a 60 ms release from the step edge
         * spends half its life inside step 1. */
        tg_core_set_param(c, "release", "60");

        /* Two steps of DC through the SPLIT path, so an index is a frame and
         * not half of one -- the interleaved path with mono indexing is how
         * the first draft of this test passed against the very bug it is
         * here to catch. */
        const int spb = (int)(44100.0 * 0.125);
        float *buf = render_dc(c, spb * 2, 120.0f);

        /* The gate opened at a quarter, so nothing it produces -- body or
         * tail -- may exceed a quarter. Before the fix the tail climbed
         * towards step 1's full level instead of decaying from step 0's. */
        float peakTail = 0.0f;
        for (int i = spb; i < spb + spb / 2; i++)
            if (buf[i] > peakTail) peakTail = buf[i];
        check_near("the release tail stays at the struck step's level",
                   peakTail, 0.25, 0.02);
        tg_core_destroy(c);
        free(buf);
    }

    /*
     * A TIE IS ONE GATE, so it holds ONE level -- the struck step's. A level
     * that stepped mid-gate was a discontinuity in the gain, which is a click.
     */
    printf("a tie holds one level:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        tg_core_set_param(c, "rate",    "1/16");
        tg_core_set_param(c, "length",  "1");
        tg_core_set_param(c, "pattern", "3");      /* both steps on */
        tg_core_set_param(c, "ties",    "1");      /* step 0 holds through */
        tg_core_set_param(c, "attack",  "0");
        tg_core_set_param(c, "decay",   "0");
        tg_core_set_param(c, "sustain", "1");
        tg_core_set_param(c, "release", "0");
        tg_core_set_param(c, "hold",    "1");
        tg_core_set_param(c, "amount",  "1");
        tg_core_set_param(c, "cursor", "0");
        tg_core_set_param(c, "step_amount", "0.50");
        tg_core_set_param(c, "cursor", "1");
        tg_core_set_param(c, "step_amount", "1.00");

        const int spb = (int)(44100.0 * 0.125);
        float *buf = render_dc(c, spb * 2, 120.0f);
        /* Sample either side of the boundary: one gate, one level. */
        check_near("before the tie's boundary", buf[spb - 100], 0.50, 0.02);
        check_near("...and after it, unchanged", buf[spb + 100], 0.50, 0.02);
        tg_core_destroy(c);
        free(buf);
    }

    /*
     * % MODE: THE SAME SHAPE AT EVERY RATE.
     *
     * The whole claim of the feature. In ms an envelope is absolute, so
     * halving the step halves how much of it fits; in % the stages are a
     * fraction OF the step, so the picture at 1/16 and at 1/64 is the same
     * picture with a different clock.
     */
    printf("%% mode follows the step:\n");
    {
        double lvl[2];
        for (int pass = 0; pass < 2; pass++) {
            tg_core_t *c = tg_core_create(44100.0);
            tg_core_set_param(c, "rate", pass == 0 ? "1/16" : "1/64");
            tg_core_set_param(c, "length",  "0");    /* one step, repeating */
            tg_core_set_param(c, "pattern", "1");
            tg_core_set_param(c, "ties",    "0");
            tg_core_set_param(c, "time_mode", "1");  /* % of the step */
            tg_core_set_param(c, "attack",  "0");
            tg_core_set_param(c, "decay",   "250");  /* 250/500 -> 50% of it */
            tg_core_set_param(c, "sustain", "0");
            tg_core_set_param(c, "release", "0");
            tg_core_set_param(c, "hold",    "1");
            tg_core_set_param(c, "amount",  "1");

            const double stepS = (pass == 0) ? 0.125 : 0.125 / 4.0;
            const int spb = (int)(44100.0 * stepS);
            float *buf = render_dc(c, spb, 120.0f);
            /* A quarter of the way in, a 50%-of-step decay is half spent. */
            lvl[pass] = buf[spb / 4];
            tg_core_destroy(c);
            free(buf);
        }
        printf("      1/16 %.3f   1/64 %.3f\n", lvl[0], lvl[1]);
        check_near("a quarter into the step reads the same at 1/16 and 1/64",
                   lvl[1], lvl[0], 0.03);
    }

    /* A patch written before % mode existed says nothing about it, and ms is
     * what those patches meant. */
    printf("an old patch still means milliseconds:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        tg_core_set_param(c, "state",
            "{\"sv\":3,\"slot\":0,\"rate\":\"1/16\",\"attack\":2.00,"
            "\"decay\":20.00,\"sustain\":1.000,\"release\":20.00,"
            "\"hold\":1.000,\"amount\":1.000,\"legato\":0}");
        char buf[16];
        tg_core_get_param(c, "time_mode", buf, sizeof(buf));
        check("a blob with no tmode loads as ms", atoi(buf) == 0);
        tg_core_destroy(c);
    }

    /*
     * THE CURVE SHAPES. Every stage's path depends on three properties, so
     * they are asserted rather than assumed: the endpoints are pinned (a
     * stage must still start and end where it did), the shape is monotonic
     * (a gate that dipped mid-attack would be a fault nobody would look for
     * here), and the inverse is exact (it is what keeps a mid-gate change
     * from clicking).
     */
    printf("envelope curve shapes:\n");
    {
        const int curves[] = { 0, 1, 2 };
        const char *names[] = { "linear", "exponential", "s-curve" };
        int ends = 1, mono = 1, inv = 1;
        double worstInv = 0.0;

        for (int ci = 0; ci < 3; ci++) {
            const int c = curves[ci];
            if (tg_test_shape(c, 0.0) != 0.0 || tg_test_shape(c, 1.0) != 1.0) ends = 0;
            double prev = -1.0;
            for (int i = 0; i <= 1000; i++) {
                const double t = (double)i / 1000.0;
                const double w = tg_test_shape(c, t);
                if (w < prev - 1e-12) mono = 0;
                prev = w;
                const double back = tg_test_shape_inv(c, w);
                const double e = fabs(back - t);
                if (e > worstInv) worstInv = e;
                if (e > 1e-6) inv = 0;
            }
            (void)names[ci];
        }
        check("every shape runs 0 to 1 exactly", ends);
        check("every shape is monotonic over 1000 points", mono);
        check_near("the inverse round-trips (worst error x1e6)",
                   worstInv * 1e6, 0.0, 1.0);
        check("...so a mid-gate curve change can re-anchor exactly", inv);

        /* The shapes are what they claim: exponential rises FASTER than
         * linear early (it eases into its target), and the s-curve is slower
         * than linear in its first half and faster in its second. */
        check("exponential is ahead of linear at the midpoint",
              tg_test_shape(1, 0.5) > 0.6);
        check("the s-curve starts behind linear", tg_test_shape(2, 0.25) < 0.25);
        check("...and finishes ahead of it",      tg_test_shape(2, 0.75) > 0.75);
        printf("      exp(0.5)=%.3f  s(0.25)=%.3f  s(0.75)=%.3f\n",
               tg_test_shape(1, 0.5), tg_test_shape(2, 0.25), tg_test_shape(2, 0.75));
    }

    /*
     * CHANGING THE CURVE UNDER A LIVE GATE MUST NOT STEP THE GAIN. The whole
     * reason env_t is re-anchored rather than left alone; without it a swap
     * mid-attack moves the level from 0.50 to 0.82 in one sample.
     */
    printf("a curve change mid-gate is silent:\n");
    {
        double worst = 0.0;
        for (int from = 0; from < 3; from++) {
            for (int to = 0; to < 3; to++) {
                if (from == to) continue;
                tg_core_t *c = tg_core_create(44100.0);
                tg_core_set_param(c, "rate",    "1/4");    /* a long step */
                tg_core_set_param(c, "length",  "0");
                tg_core_set_param(c, "pattern", "1");
                tg_core_set_param(c, "ties",    "0");
                tg_core_set_param(c, "attack",  "200");    /* mid-attack when we swap */
                tg_core_set_param(c, "decay",   "0");
                tg_core_set_param(c, "sustain", "1");
                tg_core_set_param(c, "release", "0");
                tg_core_set_param(c, "hold",    "1");
                tg_core_set_param(c, "amount",  "1");
                { char v[8]; snprintf(v, sizeof(v), "%d", from);
                  tg_core_set_param(c, "curve", v); }

                /* Run a quarter of a second in, landing inside the attack. */
                float *a = render_dc(c, 4410, 120.0f);
                const float before = a[4409];
                free(a);

                { char v[8]; snprintf(v, sizeof(v), "%d", to);
                  tg_core_set_param(c, "curve", v); }

                float *b = render_dc(c, 64, 120.0f);
                const double jump = fabs((double)b[0] - (double)before);
                if (jump > worst) worst = jump;
                free(b);
                tg_core_destroy(c);
            }
        }
        printf("      worst jump across all six swaps: %.6f\n", worst);
        check("no swap moves the gain by more than 1e-3", worst < 1e-3);
    }

    /* A patch written before curves existed says nothing about them, and
     * straight lines are what those patches sounded like. */
    printf("an old patch still means straight lines:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        tg_core_set_param(c, "curve", "2");
        tg_core_set_param(c, "state",
            "{\"sv\":3,\"slot\":0,\"rate\":\"1/16\",\"attack\":2.00,"
            "\"decay\":20.00,\"sustain\":1.000,\"release\":20.00,"
            "\"hold\":1.000,\"amount\":1.000,\"legato\":0}");
        char buf[16];
        tg_core_get_param(c, "curve", buf, sizeof(buf));
        check("a blob with no curve key loads as linear", atoi(buf) == 0);
        tg_core_destroy(c);
    }

    /*
     * A NEW GATE STARTS WHERE THE GAIN IS, NOT WHERE `env` IS.
     *
     * What you hear is env * level, and the level changes at the very
     * boundary a new gate opens on. att_from carried `env` across, so after a
     * half-filled pad the envelope resumed at the right ENV and instantly the
     * wrong GAIN.
     *
     * Two steps both fully open (Width 100%, so the gate never closes inside
     * a step) at different amounts is the smallest case: the gain at the
     * boundary must not step.
     */
    printf("a new gate picks up the gain it inherits:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        tg_core_set_param(c, "rate",    "1/16");
        tg_core_set_param(c, "length",  "1");      /* two steps */
        tg_core_set_param(c, "pattern", "3");      /* both on */
        tg_core_set_param(c, "ties",    "0");      /* ...and NOT tied: it retriggers */
        tg_core_set_param(c, "attack",  "50");
        tg_core_set_param(c, "decay",   "0");
        tg_core_set_param(c, "sustain", "1");
        tg_core_set_param(c, "release", "0");
        tg_core_set_param(c, "hold",    "1");      /* open for the whole step */
        tg_core_set_param(c, "amount",  "1");
        tg_core_set_param(c, "cursor", "0");
        tg_core_set_param(c, "step_amount", "1.00");
        tg_core_set_param(c, "cursor", "1");
        tg_core_set_param(c, "step_amount", "0.25");

        const int spb = (int)(44100.0 * 0.125);
        float *buf = render_dc(c, spb * 2, 120.0f);

        /* Either side of the boundary, a couple of samples clear of it. */
        const double before = buf[spb - 4];
        const double after  = buf[spb + 4];
        printf("      gain before %.3f, after %.3f\n", before, after);
        check_near("the gain does not step at the retrigger", after, before, 0.02);

        /* ...and it then RAMPS to the new step's level over the attack,
         * rather than sitting where it was. Halfway through 50 ms it is on
         * its way down from 1.00 to 0.25. */
        const double mid = buf[spb + (int)(44100.0 * 0.025)];
        check("...then ramps down to the new level", mid < before - 0.1 && mid > 0.25);

        free(buf);
        tg_core_destroy(c);
    }

    /*
     * A STAGE IS A PERCENTAGE OF THE GATE'S WIDTH.
     *
     * 100% exactly fills the gate, 200% is twice it. The measurable form:
     * how many samples a stage takes, read off the point the envelope
     * finishes rising.
     */
    printf("stages are measured against Width:\n");
    {
        /*
         * MEASURED BY THE RAMP'S SLOPE, NOT BY WHEN IT FINISHES.
         *
         * A 200% stage cannot finish -- that is what 200% MEANS, twice the
         * gate -- so timing its arrival at full open measures the window the
         * test happened to render and nothing else. A linear attack is at
         * t/duration, so sampling a quarter of the way in reads 0.25 whether
         * the stage completes or not.
         *
         * FROM A CLOSED GATE, which is why the attack under test is step 1's
         * and step 0 is off. The transport starts with the gate OPEN and the
         * envelope seeded there, so the very first step no longer ramps up
         * from silence -- that drop was a click -- and a ramp measured on it
         * would be measuring from the wrong floor.
         */
        const int step = (int)(44100.0 * 60.0 / 120.0 / 4.0);   /* 5512 */
        struct { const char *pct; double hold; double want_ms; } cases[] = {
            { "100", 1.00, 125.0 },   /* a full-width 1/16 step at 120 BPM */
            { "50",  1.00,  62.5 },
            { "100", 0.50,  62.5 },   /* half the Width -> half the stage */
            { "200", 0.50, 125.0 },
        };
        for (int i = 0; i < 4; i++) {
            tg_core_t *c = tg_core_create(44100.0);
            tg_core_set_param(c, "rate",    "1/16");
            tg_core_set_param(c, "length",  "1");   /* two steps: off, on */
            tg_core_set_param(c, "pattern", "2");
            tg_core_set_param(c, "ties",    "0");
            tg_core_set_param(c, "decay",   "0");
            tg_core_set_param(c, "sustain", "1");
            tg_core_set_param(c, "release", "0");
            tg_core_set_param(c, "amount",  "1");
            tg_core_set_param(c, "curve",   "0");   /* linear: a straight ramp */
            { char v[16]; snprintf(v, sizeof(v), "%.3f", cases[i].hold);
              tg_core_set_param(c, "hold", v); }
            tg_core_set_param(c, "attack", cases[i].pct);

            /* A quarter of the way into step 1's attack. */
            const int at = step + 1 + (int)(44100.0 * cases[i].want_ms / 4000.0);
            float *buf = render_dc(c, at + 64, 120.0f);
            char what[96];
            snprintf(what, sizeof(what), "%s%% of a %.0f%% Width ramps over %.0f ms",
                     cases[i].pct, cases[i].hold * 100.0, cases[i].want_ms);
            check_near(what, buf[at], 0.25, 0.02);
            free(buf);
            tg_core_destroy(c);
        }
    }

    /*
     * CHANGING THE RATE LEAVES THE PERCENTAGE ALONE and moves what it is
     * worth in samples. This is the whole reason for measuring against Width:
     * a patch keeps its shape and only its clock changes.
     */
    printf("a rate change rescales the stage, not the number:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        tg_core_set_param(c, "rate",    "1/16");
        tg_core_set_param(c, "length",  "0");
        tg_core_set_param(c, "pattern", "1");
        tg_core_set_param(c, "ties",    "0");
        tg_core_set_param(c, "attack",  "100");
        tg_core_set_param(c, "decay",   "0");
        tg_core_set_param(c, "sustain", "1");
        tg_core_set_param(c, "release", "0");
        tg_core_set_param(c, "hold",    "1");
        tg_core_set_param(c, "amount",  "1");

        char a[16], w1[16], w2[16];
        tg_core_get_param(c, "attack", a, sizeof(a));
        tg_core_get_param(c, "width_ms", w1, sizeof(w1));
        tg_core_set_param(c, "rate", "1/32");
        char a2[16];
        tg_core_get_param(c, "attack", a2, sizeof(a2));
        tg_core_get_param(c, "width_ms", w2, sizeof(w2));

        printf("      attack %s%% -> %s%%,  width %s ms -> %s ms\n", a, a2, w1, w2);
        check("the percentage is untouched", atof(a2) == atof(a));
        check_near("...and the width halves with the rate",
                   atof(w1) / atof(w2), 2.0, 0.05);
        tg_core_destroy(c);
    }

    /*
     * A LEGACY PATCH HELD MILLISECONDS. Reinterpreting 2 ms as 2% would be a
     * patch that loads and sounds like a different patch, so a pre-v4 blob is
     * CONVERTED using its own rate and Width.
     */
    printf("a v3 patch's milliseconds convert:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        /* 1/16 at the nominal 120 BPM is 125 ms; hold 0.5 -> a 62.5 ms width.
         * A 25 ms attack is 40% of that. */
        tg_core_set_param(c, "state",
            "{\"sv\":3,\"slot\":0,\"rate\":\"1/16\",\"attack\":25.00,"
            "\"decay\":12.50,\"sustain\":1.000,\"release\":6.25,"
            "\"hold\":0.500,\"amount\":1.000,\"legato\":0}");
        char a[16], d[16], r[16];
        tg_core_get_param(c, "attack",  a, sizeof(a));
        tg_core_get_param(c, "decay",   d, sizeof(d));
        tg_core_get_param(c, "release", r, sizeof(r));
        printf("      25/12.5/6.25 ms at a 62.5 ms width -> %s / %s / %s %%\n", a, d, r);
        check_near("a 25 ms attack becomes 40%",   atof(a), 40.0, 0.5);
        check_near("a 12.5 ms decay becomes 20%",  atof(d), 20.0, 0.5);
        check_near("a 6.25 ms release becomes 10%", atof(r), 10.0, 0.5);
        tg_core_destroy(c);
    }

    /* The scope's clock: a cheap phase with no formatting in it. */
    printf("the pattern phase runs 0 to 1:\n");
    {
        for (int len = 1; len <= 128; len *= 128) {
            tg_core_t *c = tg_core_create(44100.0);
            char v[8]; snprintf(v, sizeof(v), "%d", len - 1);
            tg_core_set_param(c, "rate", "1/16");
            tg_core_set_param(c, "length", v);
            tg_core_set_param(c, "hold", "1");

            double lo = 2.0, hi = -1.0, prev = -1.0;
            int wrapped = 0;
            /* ENOUGH BLOCKS FOR A PATTERN AND A HALF. A fixed count ran 0.03s
             * of a 16-second pattern and concluded the phase never wraps,
             * which is true of any clock you do not wait for. */
            const double patternBeats = (double)len * 0.25;      /* 1/16 steps */
            const int blocks = (int)(patternBeats / 2.0 * 44100.0 * 1.5 / 32.0) + 4;
            for (int b = 0; b < blocks; b++) {
                float l[32], r[32];
                for (int i = 0; i < 32; i++) { l[i] = 0.0f; r[i] = 0.0f; }
                tg_transport_t t;
                t.running = 1; t.bpm = 120.0f;
                t.beats = (double)(b * 32) / 44100.0 * 2.0;
                tg_core_process_f32_split(c, l, r, 32, &t);
                const double ph = tg_core_phase01(c);
                if (ph < lo) lo = ph;
                if (ph > hi) hi = ph;
                if (prev >= 0.0 && ph < prev - 0.5) wrapped = 1;
                prev = ph;
            }
            char what[64];
            snprintf(what, sizeof(what), "length %d: stays inside 0..1", len);
            check(what, lo >= 0.0 && hi <= 1.0);
            snprintf(what, sizeof(what), "length %d: wraps", len);
            check(what, wrapped);
            tg_core_destroy(c);
        }
    }

    /*
     * THE FADE-IN.
     *
     * Every assertion here is read out of a RENDER: the fade's claim is about
     * what you hear, and a weight table that agrees with the readout while
     * disagreeing with the gain law would pass a test written the other way.
     */
    printf("the fade-in:\n");
    {
        /* Four hits at steps 0, 4, 8, 12 -- "1111" LSB-aligned is steps 0-3,
         * so the mask is spelled out instead. */
        const char *PAT = "1111";              /* steps 0,4,8,12 */
        float w[16];

        /* 0% is silence, and it is silence rather than "quiet": every gap is a
         * gap, so the gate is shut for the whole cycle. */
        tg_core_t *c = mk_flat(15, PAT);
        tg_core_set_param(c, "fade", "0");
        step_weights(c, 16, w);
        int any = 0;
        for (int i = 0; i < 16; i++) if (w[i] > 0.01f) any = 1;
        check("fade 0%: no step sounds", !any);
        tg_core_destroy(c);

        /* 100% is the pattern, untouched. */
        c = mk_flat(15, PAT);
        tg_core_set_param(c, "fade", "1");
        step_weights(c, 16, w);
        check("fade 100%: step 0 is full", w[0] > 0.99f);
        check("fade 100%: the last hit is full", w[12] > 0.99f);
        check("fade 100%: a gap is still a gap", w[1] < 0.01f);
        tg_core_destroy(c);

        /*
         * EQUIDISTANT ARRIVALS. Four hits, so rank r crosses at r/4: at 50%
         * exactly two sound, at 75% three, and never the other way round.
         */
        static const struct { const char *f; int want; } steps[] = {
            { "0.24", 0 }, { "0.25", 1 }, { "0.49", 1 }, { "0.50", 2 },
            { "0.74", 2 }, { "0.75", 3 }, { "0.99", 3 }, { "1.00", 4 },
        };
        for (unsigned k = 0; k < sizeof(steps) / sizeof(steps[0]); k++) {
            c = mk_flat(15, PAT);
            tg_core_set_param(c, "fade", steps[k].f);
            step_weights(c, 16, w);
            int n = 0;
            for (int i = 0; i < 16; i++) if (w[i] > 0.99f) n++;
            char what[96];
            snprintf(what, sizeof(what), "hard, fade %s: %d of 4 sound",
                     steps[k].f, steps[k].want);
            check(what, n == steps[k].want);
            tg_core_destroy(c);
        }

        /*
         * THE LAST STEP ARRIVES AT THE TOP OF THE KNOB, and this is the
         * epsilon's test. The value below is the float nearest 1.0 from
         * beneath, which is a real thing for a host to send; without the slack
         * in recalc_fade the fourth hit would stay silent there and the bug
         * would only ever appear at exactly one knob position.
         */
        c = mk_flat(15, PAT);
        tg_core_set_param(c, "fade", "0.99999994");
        step_weights(c, 16, w);
        check("hard, fade just under 1: all four still sound",
              w[0] > 0.99f && w[4] > 0.99f && w[8] > 0.99f && w[12] > 0.99f);
        tg_core_destroy(c);

        /*
         * SOFT: ONE STEP PART-WAY IN, NEVER TWO. The window is one arrival
         * wide, so at any setting the weights are some 1s, one fraction and
         * some 0s -- which is what makes the knob read as a single arriving
         * step rather than a general dimming.
         */
        for (int pct = 1; pct < 100; pct++) {
            char v[16]; snprintf(v, sizeof(v), "%.4f", pct / 100.0);
            c = mk_flat(15, PAT);
            tg_core_set_param(c, "fade_soft", "1");
            tg_core_set_param(c, "fade", v);
            step_weights(c, 16, w);
            int partial = 0;
            for (int i = 0; i < 16; i += 4)
                if (w[i] > 0.02f && w[i] < 0.98f) partial++;
            if (partial > 1) {
                printf("      fade %s: %d steps part-way in\n", v, partial);
                check("soft: at most one step is part-way in", 0);
                tg_core_destroy(c);
                break;
            }
            tg_core_destroy(c);
        }
        check("soft: at most one step is part-way in, across the travel", 1);

        /*
         * SOFT AND HARD AGREE AT EVERY ARRIVAL BOUNDARY. They are one formula
         * and a threshold, so the switch may change a step's SHAPE and must
         * never change WHEN it arrives. A soft mode that lagged by half a slot
         * would be a second feature wearing one name.
         */
        int agree = 1;
        static const char *bounds[] = { "0", "0.25", "0.50", "0.75", "1" };
        for (unsigned k = 0; k < sizeof(bounds) / sizeof(bounds[0]); k++) {
            float hard[16], soft[16];
            c = mk_flat(15, PAT);
            tg_core_set_param(c, "fade", bounds[k]);
            step_weights(c, 16, hard);
            tg_core_destroy(c);
            c = mk_flat(15, PAT);
            tg_core_set_param(c, "fade_soft", "1");
            tg_core_set_param(c, "fade", bounds[k]);
            step_weights(c, 16, soft);
            tg_core_destroy(c);
            for (int i = 0; i < 16; i += 4)
                if (fabs(hard[i] - soft[i]) > 0.02) {
                    printf("      fade %s step %d: hard %.3f soft %.3f\n",
                           bounds[k], i, hard[i], soft[i]);
                    agree = 0;
                }
        }
        check("soft and hard agree at every arrival boundary", agree);

        /*
         * SOFT ACTUALLY RAMPS. The three assertions above would all pass if
         * soft did nothing at all, so this is the one that says the fraction
         * exists: halfway through the second step's slot, it is halfway in.
         */
        c = mk_flat(15, PAT);
        tg_core_set_param(c, "fade_soft", "1");
        tg_core_set_param(c, "fade", "0.375");     /* 1.5 arrivals of 4 */
        step_weights(c, 16, w);
        check("soft: the first step is fully in", w[0] > 0.99f);
        check_near("soft: the second is half in", w[4], 0.5, 0.05);
        check("soft: the third has not started", w[8] < 0.01f);
        tg_core_destroy(c);
    }

    /*
     * FADE OUT: THE HOLES ARRIVE INSTEAD OF THE HITS.
     *
     * The knob means the same thing in both directions -- how much of the drawn
     * pattern is present -- so 100% is the pattern either way and only the
     * missing part differs. In leaves silence; Out leaves the gate open.
     */
    printf("the fade-out:\n");
    {
        const char *PAT = "1111";              /* hits 0,4,8,12; twelve holes */
        float w[16];

        /* 0% fills every hole: every step sounds, at full level. */
        tg_core_t *c = mk_flat(15, PAT);
        tg_core_set_param(c, "fade_dir", "Out");
        tg_core_set_param(c, "fade", "0");
        step_weights(c, 16, w);
        int all = 1;
        for (int i = 0; i < 16; i++) if (w[i] < 0.99f) all = 0;
        check("fade out 0%: every step sounds", all);
        tg_core_destroy(c);

        /*
         * 100% IS THE PATTERN, IN BOTH DIRECTIONS. This is what keeps 100% the
         * neutral default: switching direction at rest cannot change a sample,
         * which is also why the golden renders still hold.
         */
        float in100[16], out100[16];
        c = mk_flat(15, PAT);
        tg_core_set_param(c, "fade", "1");
        step_weights(c, 16, in100);
        tg_core_destroy(c);
        c = mk_flat(15, PAT);
        tg_core_set_param(c, "fade_dir", "Out");
        tg_core_set_param(c, "fade", "1");
        step_weights(c, 16, out100);
        tg_core_destroy(c);
        int agree = 1;
        for (int i = 0; i < 16; i++) if (fabs(in100[i] - out100[i]) > 1e-4) agree = 0;
        check("at 100% the two directions are the same pattern", agree);
        check("...which is the one that was drawn",
              out100[0] > 0.99f && out100[4] > 0.99f && out100[1] < 0.01f);

        /*
         * THE HOLES ARRIVE EQUIDISTANTLY, over twelve of them rather than four
         * hits -- rank r goes at exactly r/12. The holes run 1,2,3,5,6,7,...
         * in position order, so at 50% the first six have gone and the last six
         * still sound.
         */
        c = mk_flat(15, PAT);
        tg_core_set_param(c, "fade_dir", "Out");
        tg_core_set_param(c, "fade", "0.5");
        step_weights(c, 16, w);
        check("out 50%: the first six holes are gaps",
              w[1] < 0.01f && w[2] < 0.01f && w[3] < 0.01f &&
              w[5] < 0.01f && w[6] < 0.01f && w[7] < 0.01f);
        check("...the last six still sound",
              w[9] > 0.99f && w[10] > 0.99f && w[11] > 0.99f &&
              w[13] > 0.99f && w[14] > 0.99f && w[15] > 0.99f);
        check("...and every hit is untouched by the knob",
              w[0] > 0.99f && w[4] > 0.99f && w[8] > 0.99f && w[12] > 0.99f);
        tg_core_destroy(c);

        /* Counted across the travel: at r/12 exactly r holes have gone. */
        int bad = 0;
        for (int r = 0; r <= 12; r++) {
            char v[32];
            snprintf(v, sizeof v, "%.9g", (double) r / 12.0);
            c = mk_flat(15, PAT);
            tg_core_set_param(c, "fade_dir", "Out");
            tg_core_set_param(c, "fade", v);
            step_weights(c, 16, w);
            int gone = 0;
            for (int i = 0; i < 16; i++)
                if (i % 4 != 0 && w[i] < 0.01f) gone++;
            if (gone != r) {
                printf("      fade %s: %d holes gone, wanted %d\n", v, gone, r);
                bad++;
            }
            tg_core_destroy(c);
        }
        check("hole rank r arrives at exactly r/12", bad == 0);

        /*
         * SOFT RAMPS A HOLE'S LEVEL DOWN. That is the dual of soft fade-in: an
         * arriving hole starts as a full on step and fades to nothing, where an
         * arriving hit starts at nothing and fades up.
         */
        c = mk_flat(15, PAT);
        tg_core_set_param(c, "fade_dir", "Out");
        tg_core_set_param(c, "fade_soft", "1");
        tg_core_set_param(c, "fade", "0.125");     /* 1.5 arrivals of 12 */
        step_weights(c, 16, w);
        check("soft out: the first hole is fully gone", w[1] < 0.01f);
        check_near("soft out: the second is half way down", w[2], 0.5, 0.05);
        check("soft out: the third has not started", w[3] > 0.99f);
        tg_core_destroy(c);

        /* And the two shapes still agree at every arrival boundary. */
        int shapes = 1;
        for (int r = 0; r <= 12; r++) {
            char v[32];
            float hard[16], soft[16];
            snprintf(v, sizeof v, "%.9g", (double) r / 12.0);
            c = mk_flat(15, PAT);
            tg_core_set_param(c, "fade_dir", "Out");
            tg_core_set_param(c, "fade", v);
            step_weights(c, 16, hard);
            tg_core_destroy(c);
            c = mk_flat(15, PAT);
            tg_core_set_param(c, "fade_dir", "Out");
            tg_core_set_param(c, "fade_soft", "1");
            tg_core_set_param(c, "fade", v);
            step_weights(c, 16, soft);
            tg_core_destroy(c);
            for (int i = 0; i < 16; i++)
                if (fabs(hard[i] - soft[i]) > 0.02) shapes = 0;
        }
        check("soft and hard agree at every arrival boundary, out too", shapes);

        /*
         * A PATTERN WITH NO HOLES HAS NOTHING TO FADE, and must not divide by
         * the zero that implies -- it stays the pattern at every setting.
         */
        c = mk_flat(15, "FFFF");
        tg_core_set_param(c, "fade_dir", "Out");
        tg_core_set_param(c, "fade", "0");
        step_weights(c, 16, w);
        int solid = 1;
        for (int i = 0; i < 16; i++) if (w[i] < 0.99f) solid = 0;
        check("a pattern with no holes is unmoved by Fade Out", solid);
        tg_core_destroy(c);
    }

    /*
     * A STEP THE FADE HAS NOT REACHED IS A GAP, NOT A SILENT ON STEP.
     *
     * This is the difference that cannot be seen in a weight table. Both
     * readings render step 2 silent; only the gap reading stops step 1's TIE
     * from holding its gate open through it, and stops JOIN NEIGHBORS from
     * bridging to it. Get this wrong and the fade goes quiet a step late,
     * audibly, with the picture looking perfectly correct.
     */
    printf("a faded-out step is a gap:\n");
    {
        float w[16];
        /* Steps 0,1,2 on and adjacent; sustain below full so a retrigger is
         * audible, and Width short so a gate that is NOT held shuts early. */
        tg_core_t *c = mk_flat(15, "7");           /* steps 0,1,2 */
        tg_core_set_param(c, "sustain", "1");
        tg_core_set_param(c, "hold", "0.5");
        tg_core_set_param(c, "legato", "1");
        /* Two of three arrived, so step 2 is a gap. */
        tg_core_set_param(c, "fade", "0.67");
        step_weights(c, 16, w);
        check("the two arrived steps sound", w[0] > 0.99f && w[1] > 0.99f);
        check("the third is silent", w[2] < 0.01f);
        /*
         * AND THE SECOND STEP'S GATE SHUTS INSIDE IT. With Width 0.5 and Join
         * Neighbors on, a gate is held through only if the NEXT step sounds.
         * Step 2 does not, so step 1 must release at half a step -- which is
         * exactly the read that would be wrong if the fade masked the level
         * instead of the mask.
         */
        {
            const double SPB = 44100.0 * 60.0 / 120.0 / 4.0;
            float *buf = render_dc(c, (int)(SPB * 3), 120.0f);
            int at = (int)(SPB * 1 + SPB * 0.85);   /* late in step 1 */
            check("join neighbors does not bridge to a faded-out step",
                  buf[at] < 0.01f);
            free(buf);
        }
        tg_core_destroy(c);
    }

    /*
     * THE ARRIVAL ORDER IS ALWAYS A PERMUTATION OF 1..N.
     *
     * Nothing else in the engine has to defend against a malformed order
     * because nothing can produce one: every door that moves the mask, the
     * length or a rank normalises on the way out. These are the doors.
     */
    printf("the arrival order:\n");
    {
        tg_core_t *c = mk_flat(15, "1111");        /* steps 0,4,8,12 on */
        check("the hits are in position order",
              ui_order(c, 0) == 1 && ui_order(c, 4) == 2 &&
              ui_order(c, 8) == 3 && ui_order(c, 12) == 4);
        /*
         * AND SO ARE THE HOLES, IN THEIR OWN RANKING. This is the half the
         * direction added: a step is either a hit or a hole, never both, so one
         * array carries both orders and neither can be asked about the other.
         * Steps 1,2,3 are the first three holes.
         */
        check("the holes are in position order, among themselves",
              ui_order(c, 1) == 1 && ui_order(c, 2) == 2 && ui_order(c, 3) == 3);
        check("a hit and a hole can share a rank without colliding",
              ui_order(c, 0) == 1 && ui_order(c, 1) == 1 &&
              ui_on(c, 0) == 1 && ui_on(c, 1) == 0);

        /* A step changing kind arrives LAST in the kind it joins, and the kind
         * it left closes up behind it. */
        tg_core_set_param(c, "cursor", "2");
        tg_core_set_param(c, "step", "On");
        check("a step switched on arrives last among the hits", ui_order(c, 2) == 5);
        check("...and the holes close up behind it",
              ui_order(c, 1) == 1 && ui_order(c, 3) == 2);

        tg_core_set_param(c, "cursor", "0");
        tg_core_set_param(c, "step", "Off");
        check("a step switched off arrives last among the holes",
              ui_order(c, 0) == 12);
        check("...and the hits close up behind it",
              ui_order(c, 4) == 1 && ui_order(c, 8) == 2 &&
              ui_order(c, 12) == 3 && ui_order(c, 2) == 4);

        /* A tie does not move a step: it changes what a live step does, not
         * when it arrives. */
        tg_core_set_param(c, "cursor", "8");
        tg_core_set_param(c, "step", "Tie");
        check("On -> Tie does not reorder", ui_order(c, 8) == 2);
        tg_core_destroy(c);
    }
    {
        /*
         * SETTING A RANK SWAPS, IT DOES NOT INSERT.
         *
         * Typing a number into a step that already has one is an exchange --
         * you are naming which step arrives Nth, and the step that was Nth has
         * to go somewhere. Shifting the whole run instead would renumber every
         * step between the two, which is not what was asked for and is
         * invisible until you look at the others.
         */
        tg_core_t *c = mk_flat(15, "1111");        /* ranks 1,2,3,4 at 0,4,8,12 */
        tg_core_set_param(c, "cursor", "12");      /* holds rank 4 */
        tg_core_set_param(c, "step_order", "1");
        check("the two steps exchange ranks",
              ui_order(c, 12) == 1 && ui_order(c, 0) == 4);
        check("...and nobody between them moved",
              ui_order(c, 4) == 2 && ui_order(c, 8) == 3);
        check("step_order reads back", atoi_param(c, "step_order") == 1);

        /* Out of range clamps, and a no-op rank is a no-op. */
        tg_core_set_param(c, "step_order", "99");
        check("a rank past the end clamps to it", ui_order(c, 12) == 4);
        tg_core_set_param(c, "step_order", "0");
        check("a rank below 1 clamps to 1", ui_order(c, 12) == 1);
        tg_core_set_param(c, "step_order", "1");
        check("setting the rank it already has changes nothing",
              ui_order(c, 12) == 1 && ui_order(c, 0) == 4);

        /* A HOLE'S rank swaps within the holes, never with a hit. */
        tg_core_set_param(c, "cursor", "3");       /* the third hole */
        tg_core_set_param(c, "step_order", "1");
        check("a hole swaps within the holes",
              ui_order(c, 3) == 1 && ui_order(c, 1) == 3);
        check("...and no hit was touched",
              ui_order(c, 12) == 1 && ui_order(c, 0) == 4 &&
              ui_order(c, 4) == 2 && ui_order(c, 8) == 3);
        tg_core_destroy(c);
    }
    {
        /*
         * TAPPING A SEQUENCE STILL YIELDS THAT SEQUENCE under swap, which is
         * what lets ORDER mode keep its one rule. Each click swaps the next step
         * into the next rank, and the steps already placed hold ranks below it,
         * so none of them can be the one swapped out.
         */
        tg_core_t *c = mk_flat(15, "1111");
        static const int tap[] = { 8, 0, 12, 4 };  /* the order we want */
        for (int k = 0; k < 4; k++) {
            char v[16];
            snprintf(v, sizeof v, "%d", tap[k]);
            tg_core_set_param(c, "cursor", v);
            snprintf(v, sizeof v, "%d", k + 1);
            tg_core_set_param(c, "step_order", v);
        }
        check("tapping 8,0,12,4 gives exactly that arrival order",
              ui_order(c, 8) == 1 && ui_order(c, 0) == 2 &&
              ui_order(c, 12) == 3 && ui_order(c, 4) == 4);
        tg_core_destroy(c);
    }
    {
        /*
         * A PERMUTATION AFTER A LENGTH CHANGE TOO, and now in BOTH kinds --
         * shortening the pattern removes steps from both counts without
         * touching a single bit.
         */
        tg_core_t *c = mk_flat(15, "5555");        /* 8 hits, 8 holes */
        tg_core_set_param(c, "length", "3");       /* index -> 4 steps */
        int on_seen[8] = { 0 }, off_seen[8] = { 0 };
        int ok = 1;
        for (int i = 0; i < 4; i++) {
            const int r = ui_order(c, i);
            int *seen = ui_on(c, i) ? on_seen : off_seen;
            if (r < 1 || r > 2 || seen[r]) ok = 0;
            seen[r] = 1;
        }
        check("shortening leaves a permutation of 1..2 in each kind", ok);
        tg_core_destroy(c);
    }

    /*
     * THE PATCH BLOB CARRIES THE ORDER, AND ABSENT MEANS POSITION ORDER.
     *
     * The second half is the one that matters: every patch in existence was
     * written before this field, and all of them must load as the left-to-right
     * sweep rather than as "every step at rank 1", which would look like the
     * fade being broken.
     */
    printf("the order in the state blob:\n");
    {
        tg_core_t *c = mk_flat(15, "1111");
        /* An untouched order is NOT written -- that is what keeps a realistic
         * patch inside a bus insert's 1024 bytes. */
        char blob[TG_STATE_MAX];
        tg_core_get_param(c, "state", blob, sizeof(blob));
        check("an unreordered pattern writes no order field",
              strstr(blob, "\"p0\":\"1111:0:16:\"") != NULL);

        /* Reorder, save, load into a fresh instance, and read it back. */
        tg_core_set_param(c, "cursor", "12");
        tg_core_set_param(c, "step_order", "1");
        tg_core_get_param(c, "state", blob, sizeof(blob));
        /* Two colons: the depths are all full so that field is EMPTY, which
         * the reader handles because an absent depth already means full. */
        check("a reordered pattern does write one",
              strstr(blob, "\"p0\":\"1111:0:16::") != NULL);

        /* Ranks SWAP, so 12 and 0 exchange and the middle two do not move. */
        tg_core_t *d = tg_core_create(44100.0);
        tg_core_set_param(d, "state", blob);
        check("the hit order survives a round trip",
              ui_order(d, 12) == 1 && ui_order(d, 4) == 2 &&
              ui_order(d, 8) == 3 && ui_order(d, 0) == 4);
        tg_core_destroy(d);

        /* And so does a HOLE order, which is the half v6 added. */
        tg_core_set_param(c, "cursor", "3");
        tg_core_set_param(c, "step_order", "1");
        tg_core_get_param(c, "state", blob, sizeof(blob));
        d = tg_core_create(44100.0);
        tg_core_set_param(d, "state", blob);
        check("the hole order survives a round trip too",
              ui_order(d, 3) == 1 && ui_order(d, 1) == 3);
        tg_core_destroy(d);
        tg_core_destroy(c);

        /* A v4 blob -- the format before the fade -- loads as position order,
         * fade 1.0 and hard. */
        d = tg_core_create(44100.0);
        tg_core_set_param(d, "state",
            "{\"sv\":4,\"slot\":0,\"rate\":\"1/16\",\"attack\":0,\"decay\":0,"
            "\"sustain\":1,\"release\":0,\"hold\":1,\"amount\":1,"
            "\"legato\":0,\"tmode\":0,\"curve\":0,\"p0\":\"1111:0:16\"}");
        check("a v4 blob loads as position order",
              ui_order(d, 0) == 1 && ui_order(d, 4) == 2 &&
              ui_order(d, 8) == 3 && ui_order(d, 12) == 4);
        {
            char v[64];
            tg_core_get_param(d, "fade", v, sizeof(v));
            check("a v4 blob loads at fade 100%", atof(v) > 0.99);
            tg_core_get_param(d, "fade_soft", v, sizeof(v));
            check("a v4 blob loads hard", atoi(v) == 0);
            tg_core_get_param(d, "fade_dir", v, sizeof(v));
            check("a v4 blob loads fading IN", atoi(v) == 0);
        }
        check("a v4 blob's HOLES load in position order too",
              ui_order(d, 1) == 1 && ui_order(d, 2) == 2 && ui_order(d, 3) == 3);
        tg_core_destroy(d);

        /*
         * A v5 BLOB WROTE 00 FOR EVERY HOLE, because ranks only covered the hits
         * then. Those zeroes have to fall through to the position seed, or every
         * hole would load at rank 1 and Fade Out would arrive all at once on
         * every patch made before it existed.
         */
        d = tg_core_create(44100.0);
        tg_core_set_param(d, "state",
            "{\"sv\":5,\"slot\":0,\"rate\":\"1/16\",\"attack\":0,\"decay\":0,"
            "\"sustain\":1,\"release\":0,\"hold\":1,\"amount\":1,\"fade\":1.0000,"
            "\"fsoft\":0,\"legato\":0,\"tmode\":0,\"curve\":0,"
            "\"p0\":\"1111:0:16::04000000010000000200000003000000\"}");
        check("a v5 blob's hit order is honoured",
              ui_order(d, 0) == 4 && ui_order(d, 4) == 1 &&
              ui_order(d, 8) == 2 && ui_order(d, 12) == 3);
        check("...and its 00 holes fall back to position order",
              ui_order(d, 1) == 1 && ui_order(d, 2) == 2 && ui_order(d, 3) == 3);
        tg_core_destroy(d);
    }

    /*
     * RANDOMIZE.
     *
     * Sixteen coin flips reads as noise rather than as a gate, so what is
     * asserted here is the SHAPE of the result rather than its contents: a hit
     * on the downbeat, a plausible density, an order that is still a
     * permutation, and -- the one that would be a real bug -- a playhead that
     * does not move.
     */
    printf("randomize:\n");
    {
        /* Reproducible from a seed, which is the only reason any of the rest
         * of this can be asserted at all. */
        tg_core_t *a = mk_flat(15, "0");
        tg_core_t *b = mk_flat(15, "0");
        tg_core_set_param(a, "randomize", "12345");
        tg_core_set_param(b, "randomize", "12345");
        char pa[256], pb[256];
        tg_core_get_param(a, "state", pa, sizeof(pa));
        tg_core_get_param(b, "state", pb, sizeof(pb));
        check("the same seed gives the same pattern", strcmp(pa, pb) == 0);
        tg_core_set_param(b, "randomize", "54321");
        tg_core_get_param(b, "state", pb, sizeof(pb));
        check("a different seed gives a different one", strcmp(pa, pb) != 0);
        tg_core_destroy(a);
        tg_core_destroy(b);

        /* The shape, over many seeds -- one roll proves nothing. */
        int bad_downbeat = 0, bad_density = 0, bad_order = 0, bad_ties = 0;
        for (unsigned seed = 1; seed <= 200; seed++) {
            char v[16]; snprintf(v, sizeof(v), "%u", seed);
            tg_core_t *c = mk_flat(15, "0");
            tg_core_set_param(c, "randomize", v);

            /* FROM THE MASK, not from "order > 0" -- every step carries a rank
             * now, among its own kind, so the order cannot stand in for the
             * mask. */
            int hits = 0, seen[136] = { 0 };
            for (int i = 0; i < 16; i++) hits += ui_on(c, i) ? 1 : 0;
            if (!ui_on(c, 0)) bad_downbeat++;
            if (hits < 4 || hits > 12) bad_density++;
            /* the hits' ranks are exactly 1..hits */
            for (int i = 0; i < 16; i++) {
                if (!ui_on(c, i)) continue;
                int r = ui_order(c, i);
                if (r < 1 || r > hits || seen[r]) bad_order = 1;
                seen[r] = 1;
            }
            char t[64];
            tg_core_get_param(c, "ties", t, sizeof(t));
            if (strtol(t, NULL, 16) != 0) bad_ties++;
            tg_core_destroy(c);
        }
        check("always a hit on the downbeat", bad_downbeat == 0);
        check("a quarter to three quarters full, every time", bad_density == 0);
        check("the order is always a permutation", !bad_order);
        check("ties are cleared", bad_ties == 0);

        /*
         * IT DOES NOT DISTURB THE PLAYHEAD. Pressing this mid-bar must change
         * what the gate plays and not when: a generator that reset step_pos
         * would throw the gate out of time on every press, which is the one
         * failure a player would not forgive.
         */
        tg_core_t *c = mk_flat(15, "FFFF");
        float *junk = render_dc(c, 5000, 120.0f);
        free(junk);
        const double before = tg_core_phase01(c);
        tg_core_set_param(c, "randomize", "7");
        check_near("the playhead does not move", tg_core_phase01(c), before, 1e-9);
        tg_core_destroy(c);
    }

    /*
     * THE ORDER'S OWN WORST CASE IN THE STATE BLOB.
     *
     * The size section above measures a pattern in position order, where the
     * emitter writes no order field at all -- so it does not see this array.
     * A SHUFFLED order in all eight slots does, and it is what moved
     * TG_STATE_MAX from 4096 to 8192.
     */
    printf("state size with a shuffled order:\n");
    {
        tg_core_t *c = tg_core_create(44100.0);
        char buf[16384];
        for (int s = 0; s < 8; s++) {
            char v[16]; snprintf(v, sizeof(v), "%d", s);
            tg_core_set_param(c, "slot", v);
            tg_core_set_param(c, "length", "127");
            tg_core_set_param(c, "pattern", "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
            for (int i = 0; i < 128; i++) {
                snprintf(v, sizeof(v), "%d", i);
                tg_core_set_param(c, "cursor", v);
                tg_core_set_param(c, "step_amount", "0.5");
            }
            snprintf(v, sizeof(v), "%d", s + 1);
            tg_core_set_param(c, "randomize", v);
            /* randomize resets the depths, so re-accent AFTER it to reach the
             * real worst case: every depth off-default AND a shuffled order. */
            for (int i = 0; i < 128; i++) {
                snprintf(v, sizeof(v), "%d", i);
                tg_core_set_param(c, "cursor", v);
                tg_core_set_param(c, "step_amount", "0.5");
            }
        }
        int n = tg_core_get_param(c, "state", buf, sizeof(buf));
        printf("      8 slots x 128, accented AND shuffled : %5d bytes\n", n);
        check("the real worst case fits TG_STATE_MAX", n > 0 && n < TG_STATE_MAX);
        check("...and Schwung's audio FX cap", n > 0 && n <= 8192);
        tg_core_destroy(c);
    }

    printf(failures ? "\nFAILED (%d)\n" : "\nPASS\n", failures);
    return failures ? 1 : 0;
}

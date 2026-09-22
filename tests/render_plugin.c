/*
 * Render the gate through the PLUGIN's audio path and write s16le stereo.
 *
 * The point is an A/B against the Move module's reference render. Same patch,
 * same input, same fake transport -- but float, split channels, and the
 * transport arriving as a struct the way a DAW supplies it, rather than
 * int16 interleaved from two host callbacks.
 *
 * If this diverges from the module's render, the port changed the sound, and
 * the difference is what says where.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "trance_gate_core.h"

#define SR    44100.0
#define BLOCK 128

/*
 * FNV-1a over the rendered bytes, so the A/B can be a REGISTERED TEST rather
 * than a pipe into md5 that somebody has to remember to type. The expected
 * value is the Move module's own reference render -- the same 4 seconds
 * tests/render_ref.c in the engine repo produces, whose md5 is
 * b208becc62657c9748247b9daa7b0362.
 *
 * If this fires, the port changed the sound. Pipe both renderers to files and
 * `cmp` them: the first differing byte says which step.
 */
#define GOLDEN_FNV1A 0x37792113E4834F69ULL

static uint64_t fnv = 0xcbf29ce484222325ULL;
static void fnv_add(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { fnv ^= b[i]; fnv *= 0x100000001b3ULL; }
}

int main(int argc, char **argv) {
    /* --verify hashes instead of writing, so the test needs no golden file
     * and no shell. Without it this still streams raw s16le for a listen. */
    int verify = (argc > 1 && strcmp(argv[1], "--verify") == 0);
    double seconds = (argc > 1 && !verify) ? atof(argv[1]) : 4.0;

    tg_core_t *c = tg_core_create(SR);

    /* The identical patch tests/render_ref.c sets on the module. */
    tg_core_set_param(c, "rate",    "1/16");
    tg_core_set_param(c, "length",  "15");
    tg_core_set_param(c, "pattern", "BEEF");
    tg_core_set_param(c, "ties",    "0022");
    tg_core_set_param(c, "attack",  "3.5");
    tg_core_set_param(c, "decay",   "40");
    tg_core_set_param(c, "sustain", "0.6");
    tg_core_set_param(c, "release", "25");
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

    tg_transport_t t;
    t.running = 1;
    t.beats = 0.0;
    t.bpm = 123.0f;

    for (int done = 0; done < total; done += BLOCK) {
        int n = (total - done) < BLOCK ? (total - done) : BLOCK;
        for (int i = 0; i < n; i++) {
            /* The module's renderer quantises to int16 BEFORE gating, because
             * that is the buffer Move hands it. Matching that exactly here is
             * what makes the two comparable at all -- otherwise this would be
             * measuring int16 rounding, not the port. */
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
        t.beats += (n / SR) * (t.bpm / 60.0);
    }
    tg_core_destroy(c);

    if (verify) {
        if (fnv == GOLDEN_FNV1A) {
            printf("  4s through the plugin path matches the Move render    ok\n");
            return 0;
        }
        printf("  RENDER CHANGED: got 0x%016llX want 0x%016llX\n",
               (unsigned long long)fnv, (unsigned long long)GOLDEN_FNV1A);
        return 1;
    }
    return 0;
}

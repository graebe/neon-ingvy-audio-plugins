// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Tests for the C ABI -- compiled against sc_core.h and linked to the real
 * library, which is the ONLY thing keeping that hand-written header honest.
 *
 * WHAT THIS FILE IS FOR, AND WHAT IT IS NOT. The DSP's behaviour is covered in
 * Rust (`cargo test -p sc-core`, 48 cases), where the stage machine's state
 * is reachable and a property can be stated directly. Repeating that here
 * would be a second, worse copy of it.
 *
 * What only this file can do is prove the BOUNDARY: that every symbol the
 * header declares exists with the signature it declares, that a null or a
 * garbage argument from C is survived rather than dereferenced, and that the
 * readouts the UI parses have the shape the header documents. A mismatched
 * signature links fine and corrupts the stack at the call, so nothing short of
 * compiling against the header and running it catches one.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "sc_core.h"

static int failures = 0;
static void check(const char *what, int ok) {
    printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}
static void check_near(const char *what, double got, double want, double tol) {
    int ok = fabs(got - want) <= tol;
    printf("  %-62s %s (got %g want %g)\n", what, ok ? "ok" : "FAIL", got, want);
    if (!ok) failures++;
}

static int count_fields(const char *text) {
    int n = 1;
    for (const char *p = text; *p; p++) if (*p == ':') n++;
    return n;
}

static const unsigned char NOTE_ON[3]  = { 0x90, 36, 127 };
static const unsigned char ALL_OFF[3]  = { 0xB0, 123, 0 };

int main(void)
{
    /* ---------------------------------------------------------------- */
    printf("every declared symbol exists and the boundary holds\n");
    {
        sc_core_t *c = sc_core_create(48000.0);
        check("create returns an instance", c != NULL);
        check_near("get_sample_rate reports what create was given",
                   sc_core_get_sample_rate(c), 48000.0, 0.0);
        sc_core_set_sample_rate(c, 44100.0);
        check_near("set_sample_rate takes", sc_core_get_sample_rate(c), 44100.0, 0.0);
        sc_core_destroy(c);
    }

    /*
     * EVERY ENTRY POINT SURVIVES A NULL INSTANCE.
     *
     * Not defensive tidiness: a host that has failed to allocate, or one
     * calling into a plugin mid-teardown, hands over exactly this. Crashing
     * there takes the whole DAW down and the session with it.
     */
    {
        float l = 0.0f, r = 0.0f;
        int16_t i16[2] = { 0, 0 };
        char buf[64];
        sc_transport_t t = { 1, 0.0, 120.0f };

        sc_core_destroy(NULL);
        sc_core_set_sample_rate(NULL, 48000.0);
        sc_core_reset(NULL);
        sc_core_push_key_f32(NULL, &l, &r, 1);
        sc_core_set_key_connected(NULL, 1);
        sc_core_on_midi(NULL, NOTE_ON, 3, 0);
        sc_core_process_f32_split(NULL, &l, &r, 1, &t);
        sc_core_process_f32(NULL, &l, 1, &t);
        sc_core_process_i16(NULL, i16, 1, &t);
        sc_core_set_num(NULL, SC_P_DEPTH, 1.0);
        check_near("get_num(NULL) is 0", sc_core_get_num(NULL, SC_P_DEPTH), 0.0, 0.0);
        check("set_param(NULL) says no", sc_core_set_param(NULL, "depth", "1") == 0);
        check("get_param(NULL) is -1", sc_core_get_param(NULL, "ui", buf, sizeof buf) == -1);
        check_near("get_sample_rate(NULL) is 0", sc_core_get_sample_rate(NULL), 0.0, 0.0);
        check_near("phase01(NULL) is 0", sc_core_phase01(NULL), 0.0, 0.0);
        /* 1.0 and not 0.0: with no instance the sweep is parked at its right
         * edge, which is what "nothing has fired" means. */
        check_near("sweep01(NULL) is 1", sc_core_sweep01(NULL), 1.0, 0.0);
        check_near("duck(NULL) is 0", sc_core_duck(NULL), 0.0, 0.0);
        check("fires(NULL) is 0", sc_core_fires(NULL) == 0);
        printf("  (no crash above is the result)\n");
    }

    /* A null or absurd argument with a VALID instance. */
    {
        sc_core_t *c = sc_core_create(48000.0);
        char buf[64];
        float l = 1.0f, r = 1.0f;
        sc_core_push_key_f32(c, NULL, &r, 1);
        sc_core_push_key_f32(c, &l, NULL, 1);
        sc_core_push_key_f32(c, &l, &r, -1);
        sc_core_push_key_f32(c, &l, &r, 0);
        sc_core_process_f32_split(c, NULL, &r, 1, NULL);
        sc_core_process_f32(c, NULL, 1, NULL);
        sc_core_process_i16(c, NULL, 1, NULL);
        sc_core_on_midi(c, NULL, 3, 0);
        sc_core_on_midi(c, NOTE_ON, 0, 0);
        sc_core_on_midi(c, NOTE_ON, 99, 0);   /* longer than any real message */
        sc_core_on_midi(c, NOTE_ON, 3, -5);   /* a negative offset            */
        check("set_param(unknown key) says no", sc_core_set_param(c, "wobble", "1") == 0);
        check("get_param(unknown key) is -1", sc_core_get_param(c, "wobble", buf, sizeof buf) == -1);
        check("get_param(NULL key) is -1", sc_core_get_param(c, NULL, buf, sizeof buf) == -1);
        check("get_param(zero buffer) is -1", sc_core_get_param(c, "ui", buf, 0) == -1);
        /* An out-of-range parameter index must be ignored, not indexed. */
        sc_core_set_num(c, (sc_param_t)SC_P_COUNT, 1.0);
        sc_core_set_num(c, (sc_param_t)-1, 1.0);
        check_near("get_num(out of range) is 0",
                   sc_core_get_num(c, (sc_param_t)SC_P_COUNT), 0.0, 0.0);
        printf("  (no crash above is the result)\n");
        sc_core_destroy(c);
    }

    /* ---------------------------------------------------------------- */
    printf("\nthe two parameter doors agree\n");
    {
        sc_core_t *c = sc_core_create(48000.0);
        /* set_param is implemented VIA set_num, so a clamp cannot exist on one
         * door and not the other -- this is what says so from C. */
        sc_core_set_param(c, "depth", "9.5");
        check_near("the string door clamps too", sc_core_get_num(c, SC_P_DEPTH), 1.0, 0.0);
        sc_core_set_param(c, "attack", "99999");
        check_near("attack clamps to 200%", sc_core_get_num(c, SC_P_ATTACK), 200.0, 0.0);
        sc_core_set_param(c, "delay", "99999");
        check_near("delay clamps to +100%", sc_core_get_num(c, SC_P_DELAY), 100.0, 0.0);
        /* AND SYMMETRICALLY NEGATIVE: an early sidechain is a whole cycle of
         * travel the other way. */
        sc_core_set_param(c, "delay", "-99999");
        check_near("delay clamps to -100%", sc_core_get_num(c, SC_P_DELAY), -100.0, 0.0);

        /* A label, not an index -- what a hand-written patch carries. */
        char buf[64];
        sc_core_set_param(c, "rate", "1/8");
        sc_core_get_param(c, "rate_label", buf, sizeof buf);
        check("the string door takes a rate label", strcmp(buf, "1/8") == 0);
        sc_core_set_param(c, "curve", "S-Curve");
        sc_core_get_param(c, "curve_label", buf, sizeof buf);
        check("...and a curve label", strcmp(buf, "S-Curve") == 0);
        sc_core_set_param(c, "source", "Sidechain");
        sc_core_get_param(c, "source_label", buf, sizeof buf);
        check("...and a source label", strcmp(buf, "Sidechain") == 0);

        /* The whole table, as a plugin declares its Rate options from it: every
         * index names the rate the string door selects by that index. */
        int agree = 1, count = 0;
        char label[32];
        for (int i = 0; sc_core_rate_label(i, label, sizeof label) > 0; i++, count++) {
            char idx[8];
            snprintf(idx, sizeof idx, "%d", i);
            sc_core_set_param(c, "rate", idx);
            sc_core_get_param(c, "rate_label", buf, sizeof buf);
            if (strcmp(buf, label) != 0) agree = 0;
        }
        check("sc_core_rate_label is the engine's table", agree && count == 12);
        check("...ending where it ends", sc_core_rate_label(count, label, sizeof label) == -1);
        sc_core_rate_label(sc_core_rate_default(), label, sizeof label);
        check("sc_core_rate_default is 1/4", strcmp(label, "1/4") == 0);

        /*
         * ATOF IS NOT str::parse. "12ms" is 12 to C and an error to Rust, and
         * a state blob written by an older build may carry the unit.
         */
        sc_core_set_param(c, "lockout", "12ms");
        check_near("atof leniency: \"12ms\" is 12", sc_core_get_num(c, SC_P_LOCKOUT), 12.0, 0.0);
        sc_core_set_param(c, "lockout", "nonsense");
        check_near("atof leniency: garbage is 0", sc_core_get_num(c, SC_P_LOCKOUT), 0.0, 0.0);
        sc_core_destroy(c);
    }

    /* ---------------------------------------------------------------- */
    printf("\nthe readouts have the shape the header documents\n");
    {
        sc_core_t *c = sc_core_create(48000.0);
        char buf[SC_STATE_MAX];

        int n = sc_core_get_param(c, "ui", buf, sizeof buf);
        check("`ui` returns a length", n > 0);
        check("`ui` is null-terminated at that length", buf[n] == '\0');
        check_near("`ui` has eleven fields (App.jsx parses by position)",
                   count_fields(buf), 11, 0);

        n = sc_core_get_param(c, "params", buf, sizeof buf);
        check_near("`params` has one field per parameter",
                   count_fields(buf), SC_P_COUNT, 0);

        n = sc_core_get_param(c, "stage_ms", buf, sizeof buf);
        check_near("`stage_ms` has four fields", count_fields(buf), 4, 0);

        /*
         * TRUNCATION TERMINATES AND IS OTHERWISE SILENT, which is the contract
         * the header states and NOT snprintf's. The property that matters is
         * the terminator: every caller here reads the buffer as a C string, so
         * an unterminated truncation is a read past the end. The return value
         * being the written length rather than the wanted one is why a caller
         * cannot size a buffer from it -- asserted so the two never get
         * quietly swapped.
         */
        char tiny[8];
        memset(tiny, 0x7f, sizeof tiny);
        int written = sc_core_get_param(c, "params", tiny, sizeof tiny);
        check("a short buffer still terminates", tiny[written] == '\0');
        check("...and never reports more than it wrote",
              written <= (int)sizeof(tiny) - 1);
        check("...and truncation is not an error", written > 0);
        sc_core_destroy(c);
    }

    /* ---------------------------------------------------------------- */
    printf("\nthe engine ducks, through C, on each source\n");
    {
        /* Cycle: a running transport is all it needs. */
        sc_core_t *c = sc_core_create(48000.0);
        sc_core_set_param(c, "rate", "1/4");
        sc_core_set_num(c, SC_P_DEPTH, 1.0);
        sc_core_set_num(c, SC_P_ATTACK, 0.0);
        sc_core_set_num(c, SC_P_HOLD, 40.0);
        float l[4800], r[4800];
        for (int i = 0; i < 4800; i++) { l[i] = 1.0f; r[i] = 1.0f; }
        sc_transport_t t = { 1, 0.0, 120.0f };
        sc_core_process_f32_split(c, l, r, 4800, &t);
        check("cycle: the transport triggers it", sc_core_fires(c) >= 1);
        check("cycle: and the audio is attenuated", l[100] < 1.0f);
        check("cycle: both channels equally", l[100] == r[100]);
        sc_core_destroy(c);

        /* MIDI: no transport at all, and the offset is honoured. */
        c = sc_core_create(48000.0);
        sc_core_set_param(c, "source", "MIDI");
        sc_core_set_num(c, SC_P_DEPTH, 1.0);
        sc_core_set_num(c, SC_P_ATTACK, 0.0);
        sc_core_set_num(c, SC_P_HOLD, 40.0);
        for (int i = 0; i < 4800; i++) { l[i] = 1.0f; r[i] = 1.0f; }
        sc_core_on_midi(c, NOTE_ON, 3, 512);
        sc_core_process_f32_split(c, l, r, 4800, NULL);
        check("midi: works with no transport", sc_core_fires(c) == 1);
        check("midi: untouched before the offset", l[511] == 1.0f);
        check("midi: ducked at the offset", l[512] < 1.0f);

        /* And a panic opens it again. */
        sc_core_on_midi(c, ALL_OFF, 3, 0);
        for (int i = 0; i < 64; i++) { l[i] = 1.0f; r[i] = 1.0f; }
        sc_core_process_f32_split(c, l, r, 64, NULL);
        check("midi: CC 123 opens the gate", l[0] == 1.0f);
        sc_core_destroy(c);

        /* Sidechain: a key hit through the aux bus. */
        c = sc_core_create(48000.0);
        sc_core_set_param(c, "source", "Sidechain");
        sc_core_set_num(c, SC_P_DEPTH, 1.0);
        sc_core_set_num(c, SC_P_ATTACK, 0.0);
        sc_core_set_num(c, SC_P_HOLD, 40.0);
        sc_core_set_num(c, SC_P_THRESHOLD, -24.0);
        static float kl[4800], kr[4800];
        for (int i = 0; i < 4800; i++) {
            const float e = i < 480 ? 1.0f - (float)i / 480.0f : 0.0f;
            kl[i] = kr[i] = 0.9f * e * e;
            l[i] = 1.0f; r[i] = 1.0f;
        }
        sc_core_set_key_connected(c, 1);
        sc_core_push_key_f32(c, kl, kr, 4800);
        sc_core_process_f32_split(c, l, r, 4800, NULL);
        check("sidechain: the key triggers it", sc_core_fires(c) == 1);
        check("sidechain: and the audio is attenuated", l[200] < 1.0f);

        /*
         * THE KEY IS CLEARED PER BLOCK, so a shell that forgets to push does
         * not get a stale trigger out of the previous block's transient.
         */
        const unsigned int before = sc_core_fires(c);
        for (int i = 0; i < 4800; i++) { l[i] = 1.0f; r[i] = 1.0f; }
        sc_core_process_f32_split(c, l, r, 4800, NULL);
        check("sidechain: no push means silence, not the last block",
              sc_core_fires(c) == before);
        sc_core_destroy(c);
    }

    /* ---------------------------------------------------------------- */
    printf("\nthe curve hooks are reachable and sane from C\n");
    {
        for (int curve = 0; curve < 3; curve++) {
            char what[80];
            snprintf(what, sizeof what, "curve %d: 0 -> 0, 1 -> 1", curve);
            check(what, sc_test_shape(curve, 0.0) == 0.0 &&
                        sc_test_shape(curve, 1.0) == 1.0);
            snprintf(what, sizeof what, "curve %d: inverse round-trips", curve);
            const double t = 0.37;
            check(what, fabs(sc_test_shape_inv(curve, sc_test_shape(curve, t)) - t) < 1e-9);
        }
        /* An index past the table is the Linear fallback, not a read past it. */
        check("an unknown curve falls back to Linear",
              sc_test_shape(99, 0.37) == sc_test_shape(0, 0.37));
    }

    printf(failures ? "\nFAILED (%d)\n" : "\nPASS\n", failures);
    return failures ? 1 : 0;
}

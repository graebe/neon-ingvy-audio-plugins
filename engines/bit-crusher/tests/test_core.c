// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Bit-Crusher's C ABI, from C: compiled against the headers cbindgen
 * writes and linked to the plugin's real archive, so a declaration that no
 * longer matches its definition fails here rather than corrupting a stack in
 * the plugin. The Rust tests (bc-core, bc-capi) hold what the answers are;
 * this holds that C reaches them through the headers as written.
 */

#include "bc_shell.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

/* The parameter whose key is `key`, or -1. */
static int param(const char *key) {
    char buf[32];
    for (int i = 0; i < bc_param_count(); i++)
        if (bc_param_key(i, buf, sizeof buf) >= 0 && strcmp(buf, key) == 0)
            return i;
    return -1;
}

static void table(void) {
    CHECK(bc_param_count() == 18);
    char buf[32];
    for (int i = 0; i < bc_param_count(); i++) {
        BcParamInfo info;
        memset(&info, 0, sizeof info);
        CHECK(bc_param_info(i, &info));
        CHECK(info.min <= info.default_value && info.default_value <= info.max);
        CHECK(bc_param_name(i, buf, sizeof buf) > 0);
        CHECK(bc_param_text(i, info.default_value, buf, sizeof buf) > 0);
    }
    CHECK(!bc_param_info(18, NULL));

    /* The filter, appended after Refresh: off by default, Slope a choice. */
    CHECK(param("filter") == 14 && param("cutoff") == 15 && param("peak") == 16 && param("slope") == 17);
    BcParamInfo f;
    CHECK(bc_param_info(param("filter"), &f) && f.kind == BcParamKind_Toggle && f.default_value == 0.0);
    CHECK(bc_param_info(param("slope"), &f) && f.kind == BcParamKind_Choice && f.choice_count == 3);
    CHECK(bc_param_choice(param("slope"), 2, buf, sizeof buf) == 5 && strcmp(buf, "48 dB") == 0);
    CHECK(bc_param_info(param("cutoff"), &f) && f.log_travel && f.default_value == 2000.0);
    double hz = 0.0;
    CHECK(bc_param_parse(param("cutoff"), "1.5k", &hz) && hz == 1500.0);

    /* The window's Refresh: a choice, saved but not automated, appended. */
    const int refresh = param("refresh");
    CHECK(refresh == 13);
    BcParamInfo r;
    CHECK(bc_param_info(refresh, &r));
    CHECK(r.kind == BcParamKind_Choice && r.choice_count == 4 && !r.automatable && r.default_value == 3.0);
    CHECK(bc_param_choice(refresh, 0, buf, sizeof buf) == 4 && strcmp(buf, "Live") == 0);
    CHECK(bc_param_choice(refresh, 3, buf, sizeof buf) == 3 && strcmp(buf, "1/s") == 0);
    CHECK(bc_param_choice(refresh, 4, buf, sizeof buf) == -1);
    CHECK(bc_refresh_period_ms(3) == 1000.0 && bc_refresh_period_ms(0) == 0.0);

    const int rate = param("rate");
    CHECK(rate == 6);
    BcParamInfo info;
    CHECK(bc_param_info(rate, &info));
    CHECK(info.kind == BcParamKind_Float && info.log_travel && !info.bipolar);
    CHECK(info.default_value == 12000.0);
    CHECK(bc_param_unit(rate, buf, sizeof buf) == 2 && strcmp(buf, "Hz") == 0);
    CHECK(bc_param_text(rate, 4410.0, buf, sizeof buf) > 0 && strcmp(buf, "4.41 kHz") == 0);

    double v = 0.0;
    CHECK(bc_param_parse(rate, "8k", &v) && v == 8000.0);
    CHECK(!bc_param_parse(rate, "fast", &v));
    CHECK(fabs(bc_param_denormalise(rate, bc_param_normalise(rate, 3000.0)) - 3000.0) < 1e-6);

    const int aa = param("anti_alias");
    CHECK(bc_param_info(aa, &info) && info.kind == BcParamKind_Toggle);
    CHECK(bc_param_text(aa, 1.0, buf, sizeof buf) == 2 && strcmp(buf, "On") == 0);
    CHECK(param("mid_rise") == 3);
}

static void curve(void) {
    float out[5];
    bc_transfer_render(0.0, 1.0, 0.0, false, out, 5);
    CHECK(out[0] == -1.0f && out[2] == 0.0f && out[4] == 1.0f);
    bc_transfer_render(0.0, 1.0, 0.0, true, out, 5);
    CHECK(out[2] == 0.5f);
    /* The filter's gain at its cutoff: Butterworth's -3 dB, raised by Peak. */
    CHECK(fabsf(bc_filter_gain(1000.0, 0.0, 1, 48000.0, 1000.0) + 3.0103f) < 0.01f);
    CHECK(fabsf(bc_filter_gain(1000.0, 12.0, 2, 48000.0, 1000.0) - 8.9897f) < 0.01f);
}

static void instance(void) {
    BcCore *core = bc_core_new(48000.0);
    CHECK(core != NULL);
    bc_core_set_num(core, param("bits"), 2.0);
    bc_core_set_num(core, param("rate"), 96000.0);
    CHECK(bc_core_num(core, param("bits")) == 2.0);
    CHECK(isnan(bc_core_num(core, 99)));

    /* A second, so the glides from the defaults have landed. */
    static float l[48000], r[48000];
    for (int i = 0; i < 48000; i++)
        l[i] = r[i] = 0.3f;
    bc_core_process_f32(core, l, r, 48000);
    CHECK(l[47999] == 0.5f && r[47999] == 0.5f);

    /* The output is bc_latency() frames late: run past it. */
    CHECK(bc_latency() == 32);
    static int16_t frames[2 * 64];
    for (int i = 0; i < 64; i++) {
        frames[2 * i] = 9000;
        frames[2 * i + 1] = (int16_t)(i < 40 ? 30000 : -9000);
    }
    bc_core_process_i16(core, frames, 64);
    CHECK(frames[2 * 63] == 16384 && frames[2 * 63 + 1] == 32767);

    /* The panic: a held sample lets go at once. */
    bc_core_set_num(core, param("rate"), 50.0);
    for (int i = 0; i < 4800; i++)
        l[i] = 0.9f;
    bc_core_process_f32(core, l, NULL, 4800);
    const uint8_t panic[3] = {0xB5, 120, 0};
    bc_core_on_midi(core, panic, 3);
    float silence[64] = {0};
    bc_core_process_f32(core, silence, NULL, 64);
    for (int i = 0; i < 64; i++)
        CHECK(silence[i] == 0.0f);

    bc_core_reset(core);
    bc_core_free(core);
}

static void shell(void) {
    BcShell *shell = bc_shell_create(44100.0);
    CHECK(shell != NULL);
    bc_shell_post_sample_rate(shell, 48000.0);
    for (int block = 0; block < 10; block++) {
        BcCore *core = bc_shell_begin(shell);
        CHECK(core != NULL);
        float l[480];
        for (int i = 0; i < 480; i++)
            l[i] = (float)sin(2.0 * 3.14159265358979323846 * 100.0 * (block * 480 + i) / 48000.0 - 0.5);
        bc_core_process_f32(core, l, NULL, 480);
        bc_shell_end(shell, 480);
    }
    static BcScope scope;
    CHECK(bc_shell_read(shell, &scope));
    CHECK(scope.triggered);
    CHECK(scope.sample_rate == 48000.0);
    CHECK(scope.dry[0] >= 0.0f && scope.dry[BC_SCOPE - 1] != 0.0f);
    bc_shell_post_reset(shell);
    bc_shell_destroy(shell);
}

int main(void) {
    table();
    curve();
    instance();
    shell();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("bc_core: all checks passed\n");
    return 0;
}

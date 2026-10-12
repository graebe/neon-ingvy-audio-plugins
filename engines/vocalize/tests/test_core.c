// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's C ABI, from C: compiled against the headers cbindgen writes
 * and linked to the plugin's real archive, so a declaration that no longer
 * matches its definition fails here rather than corrupting a stack in the
 * plugin. The Rust tests (vz-core, vz-capi) hold what the answers are; this
 * holds that C reaches them through the headers as written.
 */

#include "vz_shell.h"

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
    for (int i = 0; i < vz_param_count(); i++)
        if (vz_param_key(i, buf, sizeof buf) >= 0 && strcmp(buf, key) == 0)
            return i;
    return -1;
}

static void table(void) {
    CHECK(vz_param_count() == 10);
    char buf[32];
    for (int i = 0; i < vz_param_count(); i++) {
        NiParamInfo info;
        memset(&info, 0, sizeof info);
        CHECK(vz_param_info(i, &info));
        CHECK(info.min <= info.default_value && info.default_value <= info.max);
        CHECK(vz_param_name(i, buf, sizeof buf) > 0);
        CHECK(vz_param_text(i, info.default_value, buf, sizeof buf) > 0);
    }
    CHECK(!vz_param_info(10, NULL));
    CHECK(param("voice") == 8);
    CHECK(param("ceiling") == 9);

    const int mode = param("mode");
    CHECK(mode == 0);
    NiParamInfo info;
    CHECK(vz_param_info(mode, &info));
    CHECK(info.kind == NiParamKind_Choice && info.max == 1.0 && info.default_value == 0.0 && !info.automatable);
    NiParamInfo ceiling;
    CHECK(vz_param_info(param("ceiling"), &ceiling) && ceiling.automatable && ceiling.default_value == 1.0);
    CHECK(vz_param_label(mode, 1, buf, sizeof buf) == 8 && strcmp(buf, "Spectral") == 0);
    CHECK(vz_param_label(mode, 2, buf, sizeof buf) == -1);

    const int vowel = param("vowel");
    CHECK(vowel == 2);
    CHECK(vz_param_info(vowel, &info) && info.kind == NiParamKind_Float && !info.bipolar);
    CHECK(vz_param_text(vowel, 3.5, buf, sizeof buf) > 0 && strcmp(buf, "O>U 50 %") == 0);
    double v = 0.0;
    CHECK(vz_param_parse(vowel, "i", &v) && v == 2.0);
    CHECK(!vz_param_parse(param("vocal"), "loud", &v));

    const int size = param("size");
    CHECK(vz_param_info(size, &info) && info.bipolar && info.min == -100.0);
    CHECK(vz_param_unit(size, buf, sizeof buf) == 1 && strcmp(buf, "%") == 0);
    CHECK(fabs(vz_param_denormalise(size, vz_param_normalise(size, 30.0)) - 30.0) < 1e-9);
    CHECK(vz_param_snap(size, 30.004) == 30.0);
}

static void model(void) {
    double f[VZ_FORMANTS];
    vz_formants(NULL, 100.0, 0.0, 0.0, 50.0, f);
    CHECK(f[0] == 768.0 && f[1] == 1333.0);
    vz_formants(NULL, 100.0, 2.0, -100.0, 50.0, f);
    CHECK(fabs(f[0] - 342.0 * 1.25) < 1e-9);

    /* A voice: the textbook's, scaled, checked, and drawn. */
    VzVoice voice;
    vz_voice_textbook(&voice);
    CHECK(voice.freq[5 * 1 + 1] == 2089.0 && voice.scale == 1.0);
    for (int i = 0; i < 25; i++)
        voice.freq[i] *= 1.2;
    CHECK(vz_voice_check(&voice));
    vz_formants(&voice, 100.0, 0.0, 0.0, 50.0, f);
    CHECK(fabs(f[1] - 1333.0 * 1.2) < 1e-9);
    VzVoice bad = voice;
    bad.bandwidth[3] = NAN;
    CHECK(!vz_voice_check(&bad));
    static float silence[4800];
    VzCapture *capture = vz_capture_new();
    CHECK(vz_capture_run(capture, silence, 4800, 48000.0, &voice) == VzCaptured_Unsung);
    vz_capture_cancel(capture);
    vz_capture_free(capture);
    CHECK(vz_latency(0.0, 48000.0) == 0);
    CHECK(vz_latency(1.0, 48000.0) == 1024);
}

static void instance(void) {
    VzCore *core = vz_core_new(48000.0);
    CHECK(core != NULL);
    vz_core_set_num(core, param("vocal"), 0.0);
    CHECK(vz_core_num(core, param("vocal")) == 0.0);
    CHECK(vz_core_set_voice(core, NULL));
    CHECK(isnan(vz_core_num(core, 99)));
    CHECK(vz_core_latency(core) == 0);

    /* Half a second, so the glides from the defaults have landed: then
     * Vocal 0 is the input, bit for bit. */
    static float l[24000], r[24000];
    for (int i = 0; i < 24000; i++)
        l[i] = r[i] = 0.3f;
    vz_core_process_f32(core, l, r, 24000);
    float tone[256];
    for (int i = 0; i < 256; i++)
        tone[i] = (float)(0.5 * sin(0.05 * i));
    float copy[256];
    memcpy(copy, tone, sizeof tone);
    vz_core_process_f32(core, tone, NULL, 256);
    CHECK(memcmp(tone, copy, sizeof tone) == 0);

    int16_t frames[8] = {9000, 9000, -9000, -9000, 0, 0, 30000, 30000};
    vz_core_process_i16(core, frames, 4);
    CHECK(frames[0] == 9000 && frames[2] == -9000 && frames[6] == 30000);

    /* Spectral: one frame late, once the switch has landed. */
    vz_core_set_num(core, param("mode"), 1.0);
    vz_core_process_f32(core, l, NULL, 4800);
    CHECK(vz_core_latency(core) == 1024);

    /* The panic: whatever rang lets go at once. */
    vz_core_set_num(core, param("vocal"), 100.0);
    vz_core_set_num(core, param("resonance"), 100.0);
    for (int i = 0; i < 4800; i++)
        l[i] = (i % 3 == 0) ? 0.9f : -0.4f;
    vz_core_process_f32(core, l, NULL, 4800);
    const uint8_t panic[3] = {0xB5, 120, 0};
    vz_core_on_midi(core, panic, 3);
    float silence[64] = {0};
    vz_core_process_f32(core, silence, NULL, 64);
    for (int i = 0; i < 64; i++)
        CHECK(silence[i] == 0.0f);

    /* Bypassed, it listens; back, it resumes from what it heard -- here a
     * second of silence, so silence. */
    static float quiet[48000];
    vz_core_listen_f32(core, quiet, NULL, 48000);
    vz_core_resume(core);
    vz_core_process_f32(core, silence, NULL, 64);
    for (int i = 0; i < 64; i++)
        CHECK(silence[i] == 0.0f);

    vz_core_reset(core);
    vz_core_free(core);
}

static void shell(void) {
    VzShell *shell = vz_shell_create(44100.0);
    CHECK(shell != NULL);
    vz_shell_post_sample_rate(shell, 48000.0);
    uint32_t seed = 1;
    for (int block = 0; block < 20; block++) {
        VzCore *core = vz_shell_begin(shell);
        CHECK(core != NULL);
        float l[480];
        for (int i = 0; i < 480; i++) {
            seed = seed * 1664525u + 1013904223u;
            l[i] = (float)((seed >> 8) & 0xFFFF) / 65535.0f - 0.5f;
        }
        vz_core_process_f32(core, l, NULL, 480);
        vz_shell_end(shell, 480);
    }
    static VzPicture picture;
    CHECK(vz_shell_read(shell, &picture));
    CHECK(picture.sample_rate == 48000.0 && picture.mode == 0 && picture.latency == 0);
    CHECK(picture.hz[0] == VZ_LOWEST_HZ && picture.serial != 0);
    double top = -1000.0;
    for (int i = 0; i < VZ_POINTS; i++)
        if (picture.output_db[i] > top)
            top = picture.output_db[i];
    CHECK(top == 0.0);
    vz_shell_post_reset(shell);
    vz_shell_destroy(shell);
}

int main(void) {
    table();
    model();
    instance();
    shell();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("vz_core: all checks passed\n");
    return 0;
}

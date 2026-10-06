// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Chord-Detector's C ABI, from C: compiled against the header cbindgen
 * writes and linked to the real archive, so a declaration that no longer
 * matches its definition fails here rather than corrupting a stack in the
 * plugin. The Rust tests (cd-core, cd-capi) hold what the answers are; this
 * holds that C reaches them through the header as written.
 */

#include "cd_capi.h"

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

static void midi(CdCore *core, uint8_t status, uint8_t note, uint8_t velocity,
                 uint32_t offset) {
    const uint8_t bytes[3] = {status, note, velocity};
    cd_core_on_midi(core, bytes, sizeof bytes, offset);
}

int main(void) {
    CdShell *shell = cd_shell_create(48000.0);
    CHECK(shell != NULL);

    /* One block: A minor 7 over C, in C Ionian. */
    CdCore *core = cd_shell_begin(shell);
    CHECK(core != NULL);
    cd_core_begin_block(core, 1, 8.0, 96.0, 4, 4, 1);
    midi(core, 0x90, 48, 100, 0);
    midi(core, 0x90, 57, 100, 10);
    midi(core, 0x90, 64, 100, 20);
    midi(core, 0x90, 67, 100, 30);
    cd_core_end_block(core, 256);
    cd_shell_end(shell, 256);

    CdReading r;
    memset(&r, 0, sizeof r);
    CHECK(cd_shell_read(shell, &r));
    CHECK(r.kind == 3);
    CHECK(strcmp(r.name, "C6") == 0);
    CHECK(strcmp(r.degree, "I6") == 0);
    CHECK(strcmp(r.key_name, "C Ionian") == 0);
    CHECK(r.alternative_count == 1);
    CHECK(strcmp(r.alternatives[0], "Am7/C") == 0);
    CHECK(r.bass == 48);
    /* Notes 0-63 in the first word, 64-127 in the second. */
    CHECK(r.notes[0] == ((1ull << 48) | (1ull << 57)));
    CHECK(r.notes[1] == ((1ull << (64 - 64)) | (1ull << (67 - 64))));
    CHECK(r.sounding[0] == r.notes[0] && r.sounding[1] == r.notes[1]);
    CHECK(r.playing == 1);
    CHECK(r.bpm == 96.0);

    CdNoteEvent events[8];
    size_t n = cd_shell_drain(shell, events, 8);
    CHECK(n == 4);
    CHECK(events[0].note == 48 && events[0].velocity == 100);
    CHECK(events[3].note == 67 && events[3].at > events[0].at);

    /* The parameter table, as a shell declares from it. */
    CHECK(cd_param_count() == 7);
    CdParamInfo info;
    CHECK(cd_param_info(0, &info));
    CHECK(info.choice_count == 12 && info.default_choice == 0 && info.automatable);
    char text[32];
    CHECK(cd_param_name(0, text, sizeof text) == 3 && strcmp(text, "Key") == 0);
    CHECK(cd_param_choice(0, 6, text, sizeof text) > 0 && strcmp(text, "F#/Gb") == 0);
    CHECK(cd_param_key(3, text, sizeof text) == 4 && strcmp(text, "hold") == 0);

    /* A panic. */
    core = cd_shell_begin(shell);
    cd_core_reset(core);
    cd_core_end_block(core, 256);
    cd_shell_end(shell, 256);
    CHECK(cd_shell_read(shell, &r));
    CHECK(r.kind == 0 && r.name[0] == '\0');
    CHECK(cd_shell_drain(shell, events, 8) == 4);

    cd_shell_destroy(shell);
    if (failures == 0)
        printf("cd_core: all checks passed\n");
    return failures == 0 ? 0 : 1;
}

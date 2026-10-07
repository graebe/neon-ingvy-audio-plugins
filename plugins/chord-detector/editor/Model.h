// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What NI Chord-Detector's editor asks of its plugin (EditorModel.h has the
 * contract every editor keeps).
 *
 * THE ENGINE'S OWN TYPES. The reading, the note events and a written note are
 * the structs of engines/chord-detector/include/cd_capi.h, which cbindgen
 * writes from the Rust: the processor copies a CdReading out of the engine's
 * triple buffer and drains its event ring, and hands them over as they are.
 * Every text in them -- the chord, its words, its numeral, the notes -- is
 * already the engine's, in the key's spelling; the editor formats nothing.
 *
 * NO GROUND. This is a utility window, as Listen-In's is: it has no animated
 * ground, so it takes no rings and keeps no Motion switch.
 */
#pragma once

#include "EditorModel.h"
#include "cd_capi.h"

namespace ni::chord_detector
{

/* The host parameters, in the engine's table order (cd_param_*), which is
 * their VST3 index. */
enum Param : int
{
    tonic,
    mode,
    spelling,
    hold,
    historyView,
    historySpan,
    zoom,
    paramCount
};

class Model : public ni::ui::EditorModel
{
public:
    /* The latest reading the engine published. */
    virtual const CdReading& reading() = 0;

    /* The notes that started or stopped since the last call, oldest first,
     * at most `capacity`. Returns how many it wrote. */
    virtual int takeNotes (CdNoteEvent* out, int capacity) = 0;

    /* How a note is written in the key the parameters describe, as the
     * reading's texts write it (cd_write_note). */
    virtual CdWrittenNote write (int midi) const = 0;

    /* The key the parameters describe: its notes and signature
     * (cd_key_info). Everything the window draws of the key comes from here
     * and from write(), so a key just chosen shows at once -- the reading
     * catches up at the engine's next block, which with the host's audio off
     * is when it is switched back on. */
    virtual CdKey key() const = 0;

    int takeRings (float*, int) override { return 0; }
    bool motion() const override { return false; }
    void setMotion (bool) override {}
};

} // namespace ni::chord_detector

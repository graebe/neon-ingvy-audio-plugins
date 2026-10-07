// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What NI Chord-Detector's editor asks of its plugin (EditorModel.h has the
 * contract every editor keeps).
 *
 * PLAIN C++, NOT THE ENGINE'S HEADER. The processor reads the engine's
 * cd_capi.h structs and hands them over as these, so the editor -- and the
 * kit's own build (ui/), which runs no cargo -- needs nothing of the Rust.
 * Every text in a Reading is already the engine's, in the key's spelling; the
 * editor formats nothing.
 *
 * NO GROUND. This is a utility window, as Listen-In's is: it has no animated
 * ground, so it takes no rings and keeps no Motion switch.
 */
#pragma once

#include "EditorModel.h"
#include "Music.h"

#include <cstdint>

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

/* What the engine last published: what sounds, what it is called, and the
 * history's clock. */
struct Reading
{
    /* Changes whenever anything but the clock does. */
    std::uint32_t serial = 0xffffffffu;
    /* 0 nothing, 1 a note, 2 an interval, 3 a chord, 4 a set with no name. */
    int kind = 0;
    /* Shown because Hold kept it after release, not because it sounds. */
    bool held = false;
    bool pedal = false;
    bool playing = false;
    /* The chord's root as a pitch class, the lowest note: -1 for none. */
    int root = -1;
    int bass = -1;
    /* Its pitch classes, C at bit 0. */
    std::uint16_t pitchClasses = 0;
    /* The notes it was read from, and the notes sounding now: under Hold,
     * after release, the first are the held chord's and the second empty. */
    ni::ui::music::NoteSet notes, sounding;
    juce::String name, description, degree, notesText;
    juce::StringArray alternatives;
    /* The history's clock: now, its tempo, a bar's length and a point where a
     * bar begins, in quarters. */
    double now = 0.0, bpm = 120.0, bar = 4.0, barOrigin = 0.0;
    /* Note events the engine could not keep while nobody read them. */
    std::uint32_t dropped = 0;
};

/* A note starting (velocity above 0) or stopping, in quarters. */
struct NoteEvent
{
    double at = 0.0;
    int note = 60;
    int velocity = 0;
};

/* How a score writes a note: where it sits, its accidental, its name. */
struct WrittenNote
{
    int staffStep = 28;
    int accidental = 0;
    juce::String name;
};

/* A key, for drawing it: its seven pitch classes and its signature. */
struct KeyInfo
{
    std::uint16_t scale = 0;
    int signature = 0;
};

class Model : public ni::ui::EditorModel
{
public:
    /* The latest reading the engine published. */
    virtual const Reading& reading() = 0;

    /* The notes that started or stopped since the last call, oldest first,
     * at most `capacity`. Returns how many it wrote. */
    virtual int takeNotes (NoteEvent* out, int capacity) = 0;

    /* How a note is written in the key the parameters describe, as the
     * reading's texts write it. */
    virtual WrittenNote write (int midi) const = 0;

    /* The key the parameters describe. Everything the window draws of the key
     * comes from here and from write(), so a key just chosen shows at once --
     * the reading catches up at the engine's next block, which with the host's
     * audio off is when it is switched back on. */
    virtual KeyInfo key() const = 0;

    int takeRings (float*, int) override { return 0; }
    bool motion() const override { return false; }
    void setMotion (bool) override {}
};

} // namespace ni::chord_detector

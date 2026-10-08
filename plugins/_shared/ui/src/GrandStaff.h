// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A NoteHistory as notation: a grand staff, treble over bass, with the key
 * signature, scrolling right to left.
 *
 * PROPORTIONAL, NOT TRANSCRIBED. A note is a black head at the moment it
 * started and a thin tail for as long as it sounded; there are no note values,
 * rests or ties, because naming a rhythm is a guess and the history is a
 * record. Bar lines are the host's bars, across both staves.
 *
 *   staves      line-200, 8px between lines; the treble's top line F5, the
 *               bass's top line A3, middle C on its ledger between them.
 *               Notes from middle C up sit on the treble staff, the rest on
 *               the bass
 *   clefs, key  Bravura (Music.h), ink-muted
 *   a note      sounding: head uv, tail uv-deep; past: head ink-muted, tail
 *               line-200. Its accidental is drawn when it differs from the
 *               key signature's for that letter (a natural cancels one)
 *   names       each chord's name above the treble staff where it began, in
 *               the hint style: the latest in ink, the earlier ones ink-dim
 *
 * WHERE A NOTE SITS IS A QUESTION OF SPELLING, so the owner answers it: the
 * Writer gives a MIDI note's staff step (letter steps up from C in octave 0)
 * and its alteration, as the key would write it -- B flat on the B line, A
 * sharp on the A line. Without one, notes are written with sharps.
 */
#pragma once

#include "HistoryView.h"

#include <functional>

namespace ni::ui
{

class GrandStaff : public HistoryView
{
public:
    struct Written
    {
        /* Letter steps up from C0: 28 is middle C's line, 30 E4's. */
        int step = 28;
        /* -2 to 2: double flat to double sharp. */
        int alteration = 0;
    };
    using Writer = std::function<Written (int midi)>;

    explicit GrandStaff (const NoteHistory&);

    void setWriter (Writer);
    /* -5 to 6: flats negative, sharps positive. */
    void setSignature (int);
    int getSignature() const noexcept { return signature; }

    /* How a note is written: the Writer's answer, or sharps without one. */
    Written write (int midi) const;

    /* The height of a staff step on screen, from the treble's top line. */
    float yOfStep (int step) const;

    static constexpr float space = 8.0f;

    void paint (juce::Graphics&) override;

protected:
    float gutter() const override;

private:
    float trebleTop() const;
    /* The signature's alteration of a letter, 0 C to 6 B. */
    int signatureOf (int letter) const;

    Writer writer;
    int signature = 0;
};

} // namespace ni::ui

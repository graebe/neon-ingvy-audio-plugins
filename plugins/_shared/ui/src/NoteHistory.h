// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The notes of the last few bars, and the clock they were played on: what a
 * GrandStaff or a PianoRoll draws. A model, not a component.
 *
 * TIME IS IN QUARTER NOTES on a clock that only goes forward -- the engine's,
 * which keeps running while the host's transport stands still or loops
 * (engines/chord-detector/crates/cd-core/src/timeline.rs). The clock says where
 * "now" is, at what tempo, and where bar lines fall; the history keeps every
 * note from its start to its end, and the end of one still sounding is +inf.
 *
 * NAMES AT THE CHANGES. mark() records a chord name at a time, for a view to
 * set above the notes; a mark with the same text as the one before it is the
 * same chord, so it is not kept twice.
 *
 * BOUNDED. What ended more than `keep` quarters before now is dropped as the
 * clock moves, so an editor left open for an evening holds a few bars, not an
 * evening. The message thread's alone, like every editor model.
 */
#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <deque>
#include <limits>
#include <utility>

namespace ni::ui
{

class NoteHistory
{
public:
    struct Note
    {
        int midi = 60;
        int velocity = 100;
        double start = 0.0;
        double end = std::numeric_limits<double>::infinity();

        bool sounding() const noexcept { return std::isinf (end); }
    };

    struct Mark
    {
        double at = 0.0;
        juce::String text;
    };

    struct Clock
    {
        /* Now, in quarters; the tempo; a bar's length in quarters; a point on
         * the clock where a bar begins. */
        double now = 0.0;
        double bpm = 120.0;
        double bar = 4.0;
        double barOrigin = 0.0;
        bool playing = false;
    };

    /* `keep` quarters of history: 8 bars of 8/4 by default, the longest span
     * a view shows in the widest meter it is likely to meet. */
    explicit NoteHistory (double keep = 64.0);

    /* A note starts sounding, or stops. A stop with no matching start is
     * ignored; a start of a note already sounding is too. */
    void start (int midi, int velocity, double at);
    void stop (int midi, double at);
    /* Every sounding note stops: a panic. */
    void stopAll (double at);

    void mark (double at, const juce::String& text);

    /* How many quarters it keeps: what the longest view of it shows. */
    void setKeep (double quarters) noexcept { keep = juce::jmax (1.0, quarters); }
    double getKeep() const noexcept { return keep; }

    /* The clock moves; what is now too old goes. */
    void setClock (const Clock&);
    const Clock& clock() const noexcept { return now; }

    const std::deque<Note>& notes() const noexcept { return played; }
    const std::deque<Mark>& marks() const noexcept { return names; }

    /* The lowest and highest note that sounds at any time after `from`, or
     * { -1, -1 } when none does. */
    std::pair<int, int> range (double from) const;

    void clear();

private:
    void prune();

    double keep;
    Clock now;
    std::deque<Note> played;
    std::deque<Mark> names;
};

} // namespace ni::ui

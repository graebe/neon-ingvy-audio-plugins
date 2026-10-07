// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the Side-Chain window SAYS about what it sees: the header's state and
 * its one amber warning, the plot's caption, the hint's conventions -- each a
 * function of the model's snapshots, kept apart from the components so a test
 * can read every case without drawing one.
 *
 * THE WARNING EXISTS BECAUSE A DUCKER WHOSE TRIGGER IS NOT ARRIVING LOOKS
 * EXACTLY LIKE ONE SET TO ZERO DEPTH, and there are several ways for that to
 * happen: a sidechain with nothing patched, or patched to the track's own
 * audio; a silent track; the transport stopped; MIDI that Live will not route
 * to an audio track; a key that never crosses Threshold. One amber mark, at
 * most, in that order -- the more basic fact first.
 */
#pragma once

#include "Model.h"

#include "Info.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <optional>

#include <vector>

namespace ni::sc
{

/*
 * IS ANYTHING COMING IN AT ALL? Below this, a column's input is silence. It
 * was the web editor's wire step -- a sample crossed as one byte over -1..1,
 * so below 0.0118 (about -38 dBFS) the scope could not tell a signal from
 * silence -- and stays the line, so the verdict is the same in both editors.
 */
inline constexpr float inputFloor = 0.012f;

/* Whether any column the sweep has reached carries input above the floor.
 * An empty capture is no verdict, and says yes. */
bool hasInput (const Scope&);

/*
 * WHETHER THE TRIGGER HAS GONE QUIET: no new fire for most of a second, so the
 * warnings that depend on it do not flicker between hits. Fed the model's
 * fire counter on each frame, with the frame's time; a frame that comes late
 * makes the verdict late, never wrong the other way -- a fire is a change of
 * the counter, and a change is never missed.
 */
class TriggerWatch
{
public:
    static constexpr double quietAfterMs = 800.0;

    void observe (std::uint32_t fires, double nowMs);
    bool isQuiet (double nowMs) const;

private:
    bool seen = false;
    std::uint32_t lastFires = 0;
    double lastChangeMs = 0.0;
};

/* The window's one warning, lower case as the manual quotes it (the header
 * sets it in capitals), or empty. */
juce::String warningFor (const State&, const Buses&, bool input, bool triggerQuiet);

/* The header's state: the source, its rate on Cycle, and the stage --
 * "Cycle 1/4 · release". `sourceName` and `rateName` are the parameters'
 * own texts. */
juce::String stateLine (const juce::String& sourceName, const juce::String& rateName,
                        Source, Stage);

/* The stage's word: idle, delay, attack, hold, release. */
juce::String stageName (Stage);

/* The plot's caption, in capitals: the picture and its span, then what the
 * grey is -- or, with nothing coming in, that. */
juce::String captionFor (bool input, double msPerCycle);

/*
 * THE HINT'S THREE CONVENTIONS, and only conventions (Hint card): drag a
 * handle, Shift for fine, double-click to reset -- the layout canvas's words.
 * The web editor's bar also carried the source's catch and the stage times'
 * total; the total is a reading, and reads in the stage knobs now (Time), and
 * the catch is what the header's warning says when it bites.
 */
std::vector<ni::ui::Clause> conventions();

/*
 * A STAGE'S READOUT IN THE UNIT TIME ASKS FOR. The stages are percentages of
 * the cycle, always; Time only chooses how they read. In ms the readout is
 * the engine's own stage length ("40 ms"); in %, or before a tempo has given
 * the cycle a length, it is the parameter's text.
 */
juce::String stageText (double stageMs, bool inMs, const juce::String& percentText);

/*
 * And typing into one: "40 ms" is milliseconds and "20 %" a percentage,
 * whatever the readout shows -- the unit you type wins -- and a bare number is
 * in the unit shown. Milliseconds become the percentage of the cycle they are
 * (the inverse of the engine's stage_ms); a percentage, and anything else, is
 * the plugin's to read (getValueForText). Nothing for text with no number, or
 * for ms before the cycle has a length.
 */
std::optional<float> parseStage (const juce::RangedAudioParameter&, const juce::String& typed,
                                 bool inMs, double msPerCycle);

/* The playhead, 0..1: the sweep, wrapping on Cycle, stopping at the end of a
 * one-shot duck elsewhere. */
double playheadOf (const State&);

/* The ruler's landmark: the instant the duck reaches its floor, in ms, or 0
 * when it is not inside the cycle. */
double landmarkMs (const ShapeMarks&, double msPerCycle);

} // namespace ni::sc

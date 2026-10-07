// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Every line NI Listen-In's window says, in one place: what each control does,
 * the window's conventions, and what the status says while it is not good.
 *
 * The hint bar shows a control's line while the pointer is over it or the
 * keyboard focus is visibly on it, and each line is also the control's
 * accessible description (ni::ui::setInfo). THE FORM: "Name — what it does",
 * at most 72 characters; a constant InfoText over that does not compile.
 *
 * WHERE THE WEB EDITOR SAID IT DIFFERENTLY. It had no info lines, and its hint
 * bar named the bus and its state ("bus 3 – name kick bus", "bus 3 is taken –
 * another Listen-In holds it"). Ultraviolet's Hint states the window's
 * conventions at rest (the canvas's LI4), and a state the plugin reports is an
 * LED (LI2) -- so the status word moved to the LED's label, and the sentence
 * that said why moved to the LED's line, word for word where it could: the
 * manual's table (docs/live.md, "When the bus is not live") reads the same
 * under the pointer as it did in the bar.
 */
#pragma once

#include "Info.h"
#include "Model.h"

namespace ni::li::info
{

using ni::ui::InfoText;

inline constexpr InfoText bus { "Bus — which of the 16 buses this track is published on." };
inline constexpr InfoText name { "Name — what readers list this bus as; Enter keeps, Escape cancels." };
inline constexpr InfoText level { "Level — the input's peak; grey while it goes nowhere." };

/* The Signature's line; the Motion switch's is the kit's
 * (EditorFrame::motionInfo), the same in every window. */
inline constexpr InfoText signature { "Neon Ingvy — the publisher of this plugin." };

/*
 * The conventions, (verb, rest): what a person does in this window. ONE
 * CLAUSE, AND A SHORT ONE. The canvas proposes "pick a free bus – name it",
 * but it drew a bar with no Motion switch (its LI6, which 1.1.0 has not
 * decided); with the switch, this 360px window leaves the tips 87px, and
 * "pick a free bus" is 93. A bar that truncates is a bar whose hints are too
 * long, as the web editor said -- the fix is fewer words, not a smaller
 * font. That a bus must be free, the LED says the moment it is not; that the
 * field names the bus, its placeholder says.
 */
inline constexpr const char* pickVerb = "pick";
inline constexpr const char* pickRest = "a bus";

/* The status word, the LED's label: ui/src/lib/state.js's STATUS_TEXT. */
inline juce::String statusText (Status s)
{
    switch (s)
    {
        case Status::live:        return "listening";
        case Status::taken:       return "slot taken";
        case Status::unavailable: return "unavailable";
        case Status::idle:        break;
    }
    return "idle";
}

/*
 * The status LED's line: what the state means, for the bus `busText` (the
 * Bus parameter's text) and the name the plugin keeps, `kept`. The longest,
 * listening on bus 16 under a 31-character name, is 68 characters.
 *
 * NOT YET READABLE IN THE BAR. With the Motion switch the tips are 87px, about
 * fourteen characters, so the bar shows "Status — list…": the reason a bus is
 * not live is the screen reader's, not the eye's. Without the Ground (the
 * canvas's LI6) they would be 186px, about thirty, and these lines would be
 * cut to fit ("Status — bus 3 held elsewhere"). Which, is the owner's call.
 */
inline juce::String statusLine (Status s, const juce::String& busText, const juce::String& kept)
{
    const auto dash = juce::String::fromUTF8 (" \xe2\x80\x94 ");
    switch (s)
    {
        case Status::live:
            return "Status" + dash + "listening on bus " + busText
                   + (kept.isEmpty() ? juce::String (", unnamed.") : ", named " + kept + ".");
        case Status::taken:
            return "Status" + dash + "bus " + busText + " is taken: another Listen-In holds it.";
        case Status::unavailable:
            return "Status" + dash + "bus unavailable: the host may be sandboxed.";
        case Status::idle:
            break;
    }
    return "Status" + dash + "starting: no audio yet; press play.";
}

} // namespace ni::li::info

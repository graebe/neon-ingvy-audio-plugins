// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * What the Side-Chain window says. Status.h has the rules; the words are the
 * web editor's (App.jsx, Shaper.jsx, lib/text.js), and the manual quotes them.
 */
#include "Status.h"

#include <algorithm>
#include <cmath>

namespace ni::sc
{

bool hasInput (const Scope& scope)
{
    if (scope.count <= 0)
        return true;
    for (int i = 0; i < scope.count; ++i)
    {
        if (! scope.isSeen (i))
            continue;
        if (std::abs (scope.at (i, Scope::dryLo)) > inputFloor
            || std::abs (scope.at (i, Scope::dryHi)) > inputFloor)
            return true;
    }
    return false;
}

void TriggerWatch::observe (std::uint32_t fires, double nowMs)
{
    if (! seen || fires != lastFires)
    {
        seen = true;
        lastFires = fires;
        lastChangeMs = nowMs;
    }
}

bool TriggerWatch::isQuiet (double nowMs) const
{
    return seen && nowMs - lastChangeMs > quietAfterMs;
}

juce::String warningFor (const State& state, const Buses& buses, bool input, bool triggerQuiet)
{
    if (state.source == Source::sidechain && ! buses.keyConnected)
        return "no key routed";
    if (state.source == Source::sidechain && buses.keyIsMain)
        return "key is the input";
    /* BEFORE the trigger's: a silent track is the more basic fact. */
    if (! input)
        return "no input";
    if (! triggerQuiet)
        return {};
    switch (state.source)
    {
        case Source::cycle:     return state.advancing ? juce::String() : juce::String ("transport stopped");
        case Source::midi:      return "no midi";
        case Source::sidechain: return "no trigger";
    }
    return {};
}

juce::String stageName (Stage stage)
{
    switch (stage)
    {
        case Stage::idle:    return "idle";
        case Stage::delay:   return "delay";
        case Stage::attack:  return "attack";
        case Stage::hold:    return "hold";
        case Stage::release: return "release";
    }
    return {};
}

juce::String stateLine (const juce::String& sourceName, const juce::String& rateName,
                        Source source, Stage stage)
{
    auto line = sourceName;
    if (source == Source::cycle)
        line << " " << rateName;
    return line + juce::String::fromUTF8 (" \xc2\xb7 ") + stageName (stage);
}

juce::String captionFor (bool input, double msPerCycle)
{
    /* The picture and its span, as the PlotWell card names one ("ONE CYCLE,
     * 500 MS"); the span only once a tempo has given the cycle a length. */
    juce::String span = "ONE CYCLE";
    if (msPerCycle > 0.0)
        span << ", " << juce::roundToInt (msPerCycle) << " MS";
    return span + (input ? "   INPUT IN GREY BEHIND" : "   NOTHING REACHING THE PLUGIN");
}

std::vector<ni::ui::Clause> conventions()
{
    return { { "drag", "a handle to shape the duck" }, { "shift", "for fine" }, { "double-click", "to reset" } };
}

juce::String stageText (double stageMs, bool inMs, const juce::String& percentText)
{
    return inMs ? juce::String (juce::roundToInt (stageMs)) + " ms" : percentText;
}

std::optional<float> parseStage (const juce::RangedAudioParameter& p, const juce::String& typed,
                                 bool inMs, double msPerCycle)
{
    const auto text = typed.trim();
    if (! text.containsAnyOf ("0123456789"))
        return std::nullopt;

    const bool saysMs = text.endsWithIgnoreCase ("ms");
    const bool saysPercent = text.containsChar ('%');
    if (saysMs || (inMs && ! saysPercent))
    {
        if (! (msPerCycle > 0.0))
            return std::nullopt;
        const double percent = text.getDoubleValue() / msPerCycle * 100.0;
        return p.convertTo0to1 ((float) percent);
    }
    return p.getValueForText (text);
}

double playheadOf (const State& state)
{
    const double s = std::max (0.0, state.sweep);
    if (s < 1.0)
        return s;
    /* A cycle wraps; a one-shot duck stops at its end. */
    return state.source == Source::cycle ? std::fmod (s, 1.0) : 1.0;
}

double landmarkMs (const ShapeMarks& marks, double msPerCycle)
{
    return marks.bottom > 0.0 && marks.bottom <= 100.0 ? marks.bottom / 100.0 * msPerCycle : 0.0;
}

} // namespace ni::sc

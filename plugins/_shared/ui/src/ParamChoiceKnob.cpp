// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A knob over a long choice. ParamChoiceKnob.h has the rule.
 */
#include "ParamChoiceKnob.h"

#include <algorithm>
#include <cmath>

namespace ni::ui
{

ParamChoiceKnob::ParamChoiceKnob (juce::RangedAudioParameter& p, const juce::String& label)
    : ParamKnob (p, label)
{
    onCommit = [this] (float to)
    {
        binding().commit (stepFrom (binding().value(), to, binding().steps()));
    };
}

ParamChoiceKnob::~ParamChoiceKnob() = default;

float ParamChoiceKnob::stepFrom (float from, float to, int options)
{
    const int last = options - 1;
    if (last <= 0)
        return juce::jlimit (0.0f, 1.0f, to);

    /* Home and End, and a step that runs into an end, go to that end. */
    if (to <= 0.0f || to >= 1.0f)
        return to <= 0.0f ? 0.0f : 1.0f;

    const int at = juce::roundToInt (juce::jlimit (0.0f, 1.0f, from) * (float) last);
    const float share = (to - from) * (float) last;
    if (std::abs (share) < 1.0e-6f)
        return (float) at / (float) last;

    const int move = std::max (1, juce::roundToInt (std::abs (share)));
    const int index = juce::jlimit (0, last, at + (share > 0.0f ? move : -move));
    return (float) index / (float) last;
}

} // namespace ni::ui

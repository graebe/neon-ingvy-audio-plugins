// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A knob over a choice too long for a Select: a MIDI note of 128, a channel
 * of 17. The Select card of Ultraviolet 1.1.0 caps a list at twelve options;
 * "more than 12 want a knob with a stepped readout". This is that knob.
 *
 * IT IS A ParamKnob, with one difference: A KEY MOVES AT LEAST ONE OPTION.
 * The Knob steps by shares of its range (1 %, 10 % a page), which over 128
 * notes is about a semitone but over 17 channels is a sixth of one, so an
 * arrow on the channel knob would round back to where it was and do nothing.
 * Here a key's share is turned into whole options, in its direction and never
 * fewer than one -- an arrow is the next option, a page a tenth of the list,
 * Home and End the ends -- and still one complete edit each.
 *
 * A DRAG NEEDS NOTHING: the parameter is a choice, so whatever it is handed it
 * holds as the nearest option, and the knob shows what it holds. The readout
 * is the parameter's text ("C1", "Omni"), as on every ParamKnob.
 *
 * Message thread only.
 */
#pragma once

#include "ParamControls.h"

namespace ni::ui
{

class ParamChoiceKnob : public ParamKnob
{
public:
    ParamChoiceKnob (juce::RangedAudioParameter&, const juce::String& label);
    ~ParamChoiceKnob() override;

    /*
     * Where a key that asked for `to` from `from` lands among `options`
     * choices, both normalised: the ends exactly, else at least one option
     * that way. A choice of one or none has nowhere to go.
     */
    static float stepFrom (float from, float to, int options);

private:
    JUCE_DECLARE_NON_COPYABLE (ParamChoiceKnob)
};

} // namespace ni::ui

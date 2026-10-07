// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * One host parameter, as a control sees it: a normalised value, the plugin's
 * text for it, and edits that reach the host as gestures.
 *
 * THE CONTROLS KNOW NOTHING ABOUT PARAMETERS. A Knob turns a number from 0 to
 * 1, a Select chooses an index, a Toggle flips; what the number MEANS -- its
 * unit, its range, its steps, its default, its text -- is the parameter's, and
 * this is the one place the two meet (the web kit's Param.jsx and params
 * store, once, for every editor):
 *
 *   a drag      begin() on its first move, input() per move, end() on release
 *               -- one gesture, so a host writes one automation touch
 *   a click     commit(): one complete gesture
 *   a key       commit(): every keystroke an edit of its own
 *   a reset     reset(): the PLUGIN's default, not the bottom of the range
 *   typed text  setText(): the PLUGIN parses it, in its own units ("40 ms",
 *               "1/8T") -- the editor never guesses a unit
 *
 * THE TEXT IS THE PLUGIN'S. text() is juce::RangedAudioParameter::getText and
 * setText() its getValueForText: in the plugin both come from the Rust engine,
 * so the editor shows exactly what the host's own parameter list shows, and
 * holds no units, precisions or enum labels of its own. A parameter that
 * cannot read a text returns its current value, so a typo changes nothing.
 *
 * Underneath is juce::ParameterAttachment: a change from the host, from
 * automation or from another control reaches onChange on the message thread,
 * whichever thread made it. Message thread only.
 */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace ni::ui
{

class ParamBinding final
{
public:
    /* `onChange` is called on the message thread whenever the value changes,
     * from anywhere -- this binding's own edits included. */
    explicit ParamBinding (juce::RangedAudioParameter&, std::function<void()> onChange = {});
    ~ParamBinding();

    juce::RangedAudioParameter& parameter() const noexcept { return param; }

    /* Normalised, 0..1. */
    float value() const;
    float defaultValue() const;

    /* The plugin's text for the value now, or for any normalised value. */
    juce::String text() const;
    juce::String textFor (float normalised) const;

    /* The steps the parameter has: 0 when continuous, else its step count
     * (2 for a switch, n for a choice of n). */
    int steps() const;
    /* A choice's options, in order; empty for a continuous parameter. */
    juce::StringArray choices() const;

    /* ---- edits */
    void begin();
    void input (float normalised);
    void end();
    bool inGesture() const noexcept { return gesture; }

    void commit (float normalised);
    void reset();
    void setText (const juce::String&);

private:
    juce::RangedAudioParameter& param;
    std::function<void()> onChange;
    juce::ParameterAttachment attachment;
    bool gesture = false;

    JUCE_DECLARE_NON_COPYABLE (ParamBinding)
};

} // namespace ni::ui

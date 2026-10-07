// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A host parameter bound to a control. ParamBinding.h has the gestures.
 */
#include "ParamBinding.h"

namespace ni::ui
{

ParamBinding::ParamBinding (juce::RangedAudioParameter& p, std::function<void()> changed)
    : param (p),
      onChange (std::move (changed)),
      attachment (p, [this] (float) { if (onChange) onChange(); })
{
}

ParamBinding::~ParamBinding()
{
    /* A control destroyed mid-drag -- the editor closed under the pointer --
     * must not leave the host's gesture open forever. */
    if (gesture)
        end();
}

float ParamBinding::value() const
{
    return param.getValue();
}

float ParamBinding::defaultValue() const
{
    return param.getDefaultValue();
}

juce::String ParamBinding::text() const
{
    return textFor (value());
}

juce::String ParamBinding::textFor (float normalised) const
{
    return param.getText (juce::jlimit (0.0f, 1.0f, normalised), 1024);
}

int ParamBinding::steps() const
{
    /* JUCE says a continuous parameter has getDefaultNumParameterSteps()
     * steps (0x7fffffff); a stepped one says how many. */
    const int n = param.getNumSteps();
    return n == juce::AudioProcessorParameter::getDefaultNumParameterSteps() ? 0 : n;
}

juce::StringArray ParamBinding::choices() const
{
    return steps() > 0 ? param.getAllValueStrings() : juce::StringArray();
}

void ParamBinding::begin()
{
    jassert (! gesture);   // a drag opens one gesture, once
    if (gesture)
        return;
    gesture = true;
    attachment.beginGesture();
}

void ParamBinding::input (float normalised)
{
    jassert (gesture);     // begin() first: an edit outside a gesture is a stray automation write
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalised)));
}

void ParamBinding::end()
{
    jassert (gesture);
    if (! gesture)
        return;
    gesture = false;
    attachment.endGesture();
}

void ParamBinding::commit (float normalised)
{
    /* ParameterAttachment writes nothing when the value would not change, so
     * a click or a key that lands where the value already is records no
     * empty touch in the host. */
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalised)));
}

void ParamBinding::reset()
{
    commit (defaultValue());
}

void ParamBinding::setText (const juce::String& typed)
{
    commit (param.getValueForText (typed.trim()));
}

} // namespace ni::ui

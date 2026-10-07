// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A host parameter built from a product's table (ParamSpec.h), holding its
 * value as the plain double an iPlug2 build held.
 *
 * WHY ONE CLASS AND NOT JUCE'S FOUR. JUCE's AudioParameterInt, Choice, Bool
 * and Float each answer one of the table's kinds, and each holds its value as
 * a float from which the plain value is derived through the normalised one --
 * so a value a saved set carries (Attack 1.6000000238418581) comes back a
 * rounding away, and a state saved straight after a load is not the bytes it
 * was loaded from. This one keeps the plain double itself: a set's values are
 * set exactly (setPlain), read exactly (plain), and written back exactly, and
 * the host sees them normalised as it always does. Its int is discrete, as
 * iPlug2 declared Slot and Length (JUCE's would report a continuous int,
 * stepCount 0, and Live would draw a ramp between slots).
 *
 * THE TEXT. A choice and a toggle show their words; an integer its number; a
 * continuous value whatever the product's `text` says (the engine's format,
 * locale-free), and reads typed text through its `parse` -- nothing for text
 * that is not a value, which leaves the parameter where it was.
 *
 * Any thread for the value, as every parameter's: an atomic double.
 */
#pragma once

#include "ParamSpec.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <optional>

namespace ni
{

class Parameter final : public juce::RangedAudioParameter
{
public:
    /* A continuous parameter's text both ways, in its plain units. */
    using Text = std::function<juce::String (double plain)>;
    using Parse = std::function<std::optional<double> (const juce::String&)>;

    /* `texts` overrides the spec's words (a choice whose words are an
     * engine's table); `text` and `parse` are a continuous parameter's. */
    explicit Parameter (const ParamSpec&, juce::StringArray texts = {}, Text text = {}, Parse parse = {});

    /* The value in its own units, exactly as set. Any thread. */
    double plain() const noexcept { return value.load (std::memory_order_relaxed); }
    /* Sets the plain value -- clamped to the range, a stepped one rounded to
     * a whole number -- without telling the host. Any thread. */
    void setPlain (double) noexcept;
    /* Sets it and tells the host, as setValueNotifyingHost does: what a state
     * load and a slot switch use. Not the audio thread. */
    void setPlainNotifyingHost (double);

    const ParamSpec& spec() const noexcept { return row; }

    float getValue() const override;
    void setValue (float normalised) override;
    float getDefaultValue() const override;
    int getNumSteps() const override;
    bool isDiscrete() const override;
    bool isBoolean() const override;
    juce::String getText (float normalised, int maximumLength) const override;
    float getValueForText (const juce::String&) const override;
    juce::StringArray getAllValueStrings() const override;
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range; }

private:
    double fromNormalised (double) const noexcept;
    double toNormalised (double plain) const noexcept;
    double clampPlain (double) const noexcept;

    const ParamSpec row;
    const juce::StringArray words;
    const Text text;
    const Parse parse;
    const juce::NormalisableRange<float> range;
    std::atomic<double> value;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Parameter)
};

/* The iPlug2 builds' Bypass: a toggle the host owns, "off" and "on" as their
 * StringListParameter spelled them. */
const ParamSpec& bypassSpec();

} // namespace ni

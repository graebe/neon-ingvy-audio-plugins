// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A host parameter from a product's table. Parameter.h says what it keeps.
 */
#include "Parameter.h"

#include <cmath>

namespace ni
{

namespace
{
juce::StringArray wordsOf (const ParamSpec& spec, juce::StringArray given)
{
    if (! given.isEmpty() || spec.texts == nullptr)
        return given;
    juce::StringArray out;
    for (int i = 0; i < spec.numTexts; ++i)
        out.add (juce::String::fromUTF8 (spec.texts[i]));
    return out;
}

juce::AudioProcessorParameterWithIDAttributes attributesOf (const ParamSpec& spec)
{
    return juce::AudioProcessorParameterWithIDAttributes().withLabel (juce::String::fromUTF8 (spec.units));
}
} // namespace

const ParamSpec& bypassSpec()
{
    static const char* const offOn[] { "off", "on" };
    static const ParamSpec spec { "bypass", "Bypass", ParamSpec::Kind::toggle, 0.0, 1.0, 0.0, "", offOn, 2 };
    return spec;
}

Parameter::Parameter (const ParamSpec& spec, juce::StringArray texts, Text textOf, Parse parseText)
    : juce::RangedAudioParameter (juce::ParameterID { spec.id, 1 }, juce::String::fromUTF8 (spec.name),
                                  attributesOf (spec)),
      row (spec),
      words (wordsOf (spec, std::move (texts))),
      text (std::move (textOf)),
      parse (std::move (parseText)),
      range ((float) spec.min, (float) spec.max, spec.stepped() ? 1.0f : 0.0f),
      value (clampPlain (spec.def))
{
    jassert (spec.max > spec.min);
    jassert (row.kind == ParamSpec::Kind::continuous || row.kind == ParamSpec::Kind::integer
             || words.size() == (int) (spec.max - spec.min) + 1);
}

double Parameter::clampPlain (double v) const noexcept
{
    if (! std::isfinite (v))
        return row.def;
    v = juce::jlimit (row.min, row.max, v);
    return row.stepped() ? std::round (v) : v;
}

double Parameter::fromNormalised (double n) const noexcept
{
    return clampPlain (row.min + juce::jlimit (0.0, 1.0, n) * (row.max - row.min));
}

double Parameter::toNormalised (double plainValue) const noexcept
{
    return (plainValue - row.min) / (row.max - row.min);
}

void Parameter::setPlain (double v) noexcept
{
    value.store (clampPlain (v), std::memory_order_relaxed);
}

void Parameter::setPlainNotifyingHost (double v)
{
    setPlain (v);
    sendValueChangedMessageToListeners (getValue());
}

float Parameter::getValue() const
{
    return (float) toNormalised (plain());
}

void Parameter::setValue (float normalised)
{
    value.store (fromNormalised (normalised), std::memory_order_relaxed);
}

float Parameter::getDefaultValue() const
{
    return (float) toNormalised (clampPlain (row.def));
}

int Parameter::getNumSteps() const
{
    if (! row.stepped())
        return juce::AudioProcessor::getDefaultNumParameterSteps();
    return (int) (row.max - row.min) + 1;
}

bool Parameter::isDiscrete() const
{
    return row.stepped();
}

bool Parameter::isBoolean() const
{
    return row.kind == ParamSpec::Kind::toggle;
}

juce::String Parameter::getText (float normalised, int maximumLength) const
{
    const double v = fromNormalised (normalised);
    juce::String out;
    switch (row.kind)
    {
        case ParamSpec::Kind::choice:
        case ParamSpec::Kind::toggle:
            out = words[(int) (v - row.min)];
            break;
        case ParamSpec::Kind::integer:
            out = juce::String ((juce::int64) v);
            break;
        case ParamSpec::Kind::continuous:
            out = text ? text (v) : juce::String (v);
            break;
    }
    return maximumLength > 0 ? out.substring (0, maximumLength) : out;
}

float Parameter::getValueForText (const juce::String& typed) const
{
    const auto t = typed.trim();
    double v = plain();
    switch (row.kind)
    {
        case ParamSpec::Kind::choice:
        {
            const int i = words.indexOf (t, true);
            v = i >= 0 ? row.min + i : (t.containsOnly ("0123456789-") && t.isNotEmpty() ? (double) t.getIntValue() : v);
            break;
        }
        case ParamSpec::Kind::toggle:
            v = (t.equalsIgnoreCase (words[1]) || (t != words[0] && t.getIntValue() != 0)) ? row.max : row.min;
            break;
        case ParamSpec::Kind::integer:
            v = t.getDoubleValue();
            break;
        case ParamSpec::Kind::continuous:
            if (parse)
            {
                if (const auto parsed = parse (t))
                    v = *parsed;
            }
            else
            {
                v = t.getDoubleValue();
            }
            break;
    }
    return (float) toNormalised (clampPlain (v));
}

juce::StringArray Parameter::getAllValueStrings() const
{
    return row.kind == ParamSpec::Kind::choice || row.kind == ParamSpec::Kind::toggle ? words : juce::StringArray();
}

} // namespace ni

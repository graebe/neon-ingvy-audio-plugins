// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The controls bound to host parameters. ParamControls.h has the rules.
 */
#include "ParamControls.h"

namespace ni::ui
{

/* ----------------------------------------------------------- ParamKnob -- */

ParamKnob::ParamKnob (juce::RangedAudioParameter& p, const juce::String& label)
    : Knob (label),
      bound (p, [this] { refresh(); })
{
    onBegin = [this] { bound.begin(); };
    onInput = [this] (float v) { bound.input (v); };
    onEnd = [this] { bound.end(); };
    onCommit = [this] (float v) { bound.commit (v); };
    onReset = [this] { bound.reset(); };
    onText = [this] (const juce::String& t) { typed (t); };
    refresh();
}

ParamKnob::~ParamKnob() = default;

void ParamKnob::setText (TextSource text, TextParser parse)
{
    textSource = std::move (text);
    textParser = std::move (parse);
    refresh();
}

void ParamKnob::refresh()
{
    setValue (bound.value());
    setValueText (textSource ? textSource() : bound.text());
}

void ParamKnob::typed (const juce::String& text)
{
    if (! textParser)
    {
        bound.setText (text);
        return;
    }
    if (const auto v = textParser (text.trim()))
        bound.commit (*v);
    /* Refused or unchanged, the readout shows the plugin's text again rather
     * than the typing (Readout.h) -- and a changed value arrives through the
     * binding, which refreshes. */
    refresh();
}

/* --------------------------------------------------------- ParamSelect -- */

ParamSelect::ParamSelect (juce::RangedAudioParameter& p, const juce::StringArray& options)
    : bound (p, [this] { refresh(); })
{
    setOptions (options.isEmpty() ? bound.choices() : options);
    onChange = [this] (int index)
    {
        const int last = getOptions().size() - 1;
        bound.commit (last > 0 ? (float) index / (float) last : 0.0f);
    };
    onReset = [this] { bound.reset(); };
    refresh();
}

ParamSelect::~ParamSelect() = default;

int ParamSelect::indexOfValue() const
{
    const int last = getOptions().size() - 1;
    return last > 0 ? juce::jlimit (0, last, juce::roundToInt (bound.value() * (float) last)) : 0;
}

void ParamSelect::refresh()
{
    setIndex (indexOfValue());
}

/* --------------------------------------------------------- ParamToggle -- */

ParamToggle::ParamToggle (juce::RangedAudioParameter& p, const juce::String& label)
    : Toggle (label),
      bound (p, [this] { refresh(); })
{
    onChange = [this] (bool on) { bound.commit (on ? 1.0f : 0.0f); };
    refresh();
}

ParamToggle::~ParamToggle() = default;

void ParamToggle::refresh()
{
    setOn (bound.value() >= 0.5f);
}

} // namespace ni::ui

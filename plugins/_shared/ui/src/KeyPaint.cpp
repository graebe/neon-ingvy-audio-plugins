// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A piano key's paint. KeyPaint.h has the colours.
 */
#include "KeyPaint.h"

#include "UvTokens.h"
#include "UvType.h"

namespace ni::ui::keypaint
{

namespace c = uv::tok::colour;

juce::Colour litColour (bool dimmed)
{
    return dimmed ? c::uvDeep : c::uv;
}

void whiteKey (juce::Graphics& g, juce::Rectangle<float> key, bool on, juce::Colour lit)
{
    g.setColour (on ? lit : c::bg300);
    g.fillRect (key);
}

void blackKey (juce::Graphics& g, juce::Rectangle<float> key, bool on, juce::Colour lit)
{
    g.setColour (on ? lit : c::bg000);
    g.fillRect (key);
    g.setColour (on ? lit : c::line200);
    g.drawRect (key, uv::tok::stroke::strokeHair);
}

void keyName (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> box, bool on,
              juce::Justification just)
{
    uv::type::draw (g, name, box, uv::type::font (uv::tok::type::hint), on ? c::onUv : c::inkDim, just);
}

} // namespace ni::ui::keypaint

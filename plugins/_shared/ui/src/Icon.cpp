// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A machine glyph as a component. Icon.h has the rules.
 */
#include "Icon.h"

#include "UvIcons.h"

namespace ni::ui
{

Icon::Icon (const juce::String& g, juce::Colour t) : tint (t)
{
    setInterceptsMouseClicks (false, false);
    setAccessible (false);
    setGlyph (g);
    setSize (size, size);
}

void Icon::setGlyph (const juce::String& g)
{
    /* The set, and nothing else: a control without a glyph in it gets a word. */
    jassert (g.isEmpty() || uv::hasIcon (g));
    if (g == glyph)
        return;
    glyph = g;
    repaint();
}

void Icon::setTint (juce::Colour t)
{
    if (t == tint)
        return;
    tint = t;
    repaint();
}

void Icon::paint (juce::Graphics& g)
{
    if (glyph.isNotEmpty())
        uv::drawIcon (g, glyph, tint, getLocalBounds().toFloat());
}

} // namespace ni::ui

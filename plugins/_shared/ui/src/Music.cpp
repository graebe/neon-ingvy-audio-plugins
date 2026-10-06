// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Bravura and the glyphs. See Music.h.
 */
#include "Music.h"

#include <NiUiAssets.h>

namespace ni::ui::music
{

namespace
{
class BravuraStore final : private juce::DeletedAtShutdown
{
public:
    BravuraStore()
    {
        face = juce::Typeface::createSystemTypefaceFor (ni_ui_assets::Bravura_otf,
                                                       (size_t) ni_ui_assets::Bravura_otfSize);
        /* In this binary; a null is a platform refusing a valid OpenType
         * file, and the staff would draw nothing. */
        jassert (face != nullptr);
    }

    ~BravuraStore() override { clearSingletonInstance(); }

    juce::Typeface::Ptr face;

    JUCE_DECLARE_SINGLETON_INLINE (BravuraStore, false)
};
} // namespace

juce::juce_wchar glyph::accidental (int alteration) noexcept
{
    switch (alteration)
    {
        case -2: return doubleFlat;
        case -1: return flat;
        case 1: return sharp;
        case 2: return doubleSharp;
        default: return natural;
    }
}

juce::Typeface::Ptr bravura()
{
    JUCE_ASSERT_MESSAGE_THREAD
    return BravuraStore::getInstance()->face;
}

juce::Font bravuraFont (float space)
{
    return juce::Font (juce::FontOptions (bravura()).withPointHeight (4.0f * space));
}

void drawGlyph (juce::Graphics& g, juce::juce_wchar c, juce::Point<float> origin, float space, juce::Colour colour)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (bravuraFont (space), juce::String::charToString (c), origin.x, origin.y);
    g.setColour (colour);
    glyphs.draw (g);
}

float glyphWidth (juce::juce_wchar c, float space)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (bravuraFont (space), juce::String::charToString (c), 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, true).getWidth();
}

} // namespace ni::ui::music

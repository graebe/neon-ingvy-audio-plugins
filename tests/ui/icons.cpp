// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The fifteen glyphs: exactly the design's set, each on its 16px grid, and
 * drawn in the colour asked for -- never the file's own ink.
 */
#include "UvIcons.h"
#include "UvTokens.h"

#include <doctest.h>

namespace c = uv::tok::colour;

namespace
{
/* The glyph drawn at 1x into a transparent 16px square. */
juce::Image draw (const juce::String& name, juce::Colour colour)
{
    juce::Image img (juce::Image::ARGB, 16, 16, true, juce::SoftwareImageType());
    juce::Graphics g (img);
    uv::drawIcon (g, name, colour, { 0.0f, 0.0f, 16.0f, 16.0f });
    return img;
}
} // namespace

TEST_CASE ("icons: the set is the design's fifteen, in its order")
{
    const juce::StringArray want { "play", "pause", "stop", "record", "loop", "copy", "paste", "export",
                                   "export-all", "import", "shuffle", "reset", "link", "chevron", "power" };
    CHECK (uv::iconNames() == want);

    /* And the mirror has no glyph the kit does not know: a sixteenth would be
     * a new release of the system, to be added here on purpose. */
    juce::StringArray onDisk;
    for (const auto& f : juce::File (NI_ROOT).getChildFile ("design/scheme/project/assets/Icons")
                             .findChildFiles (juce::File::findFiles, false, "*.svg"))
        onDisk.add (f.getFileNameWithoutExtension());
    onDisk.sort (false);
    auto sorted = want;
    sorted.sort (false);
    CHECK (onDisk == sorted);
}

TEST_CASE ("icons: every glyph loads, on its 16px grid")
{
    for (const auto& name : uv::iconNames())
    {
        CAPTURE (name);
        const auto glyph = uv::icon (name, c::ink);
        REQUIRE (glyph != nullptr);
        CHECK (glyph->getContentBounds() == juce::Rectangle<float> (0.0f, 0.0f, 16.0f, 16.0f));
        const auto drawn = glyph->getDrawableBounds();
        CHECK (juce::Rectangle<float> (-1.0f, -1.0f, 18.0f, 18.0f).contains (drawn));
    }
}

TEST_CASE ("icons: a glyph is drawn in its control's colour and nothing else")
{
    for (const auto colour : { c::onUv, c::inkDim, c::bg000 })
    {
        const auto img = draw ("copy", colour);
        int inked = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
            {
                const auto p = img.getPixelAt (x, y);
                if (p.getAlpha() == 0)
                    continue;
                ++inked;
                /* The glyph's body is the colour; a faint edge pixel's
                 * channels are only as exact as its alpha lets them be. */
                if (p.getAlpha() < 128)
                    continue;
                CHECK (std::abs ((int) p.getRed() - (int) colour.getRed()) <= 2);
                CHECK (std::abs ((int) p.getGreen() - (int) colour.getGreen()) <= 2);
                CHECK (std::abs ((int) p.getBlue() - (int) colour.getBlue()) <= 2);
            }
        CHECK (inked > 20);   // and the glyph is there at all
    }
}

TEST_CASE ("icons: play and record are filled, the rest are strokes")
{
    CHECK (uv::isFilledIcon ("play"));
    CHECK (uv::isFilledIcon ("record"));
    CHECK_FALSE (uv::isFilledIcon ("stop"));

    /* The middle of a filled circle is ink; the middle of a stroked square
     * is not. */
    CHECK (draw ("record", c::ink).getPixelAt (8, 8).getAlpha() == 255);
    CHECK (draw ("stop", c::ink).getPixelAt (8, 8).getAlpha() == 0);
}

TEST_CASE ("icons: a kept glyph is recoloured in place")
{
    auto glyph = uv::icon ("export", c::ink);
    REQUIRE (glyph != nullptr);
    uv::tint (*glyph, "export", c::onUv);

    juce::Image img (juce::Image::ARGB, 16, 16, true, juce::SoftwareImageType());
    {
        juce::Graphics g (img);
        glyph->draw (g, 1.0f);
    }
    bool any = false;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            if (img.getPixelAt (x, y).getAlpha() == 255)
            {
                any = true;
                CHECK (img.getPixelAt (x, y).getRed() == c::onUv.getRed());
            }
    CHECK (any);
}

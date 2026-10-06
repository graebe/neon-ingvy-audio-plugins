// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The window ground at rest: one 96px tile of bg-000, dot paper on the
 * multiples of 12, and the design's grain in uv-deep -- repeating exactly, and
 * lined up with the window wherever a part of it is painted.
 */
#include "UvGround.h"
#include "UvTokens.h"

#include <doctest.h>

namespace c = uv::tok::colour;

namespace
{
/* How far a pixel is from bg-000 towards `to`, 0..1, by its largest channel. */
float towards (juce::Colour p, juce::Colour to)
{
    const auto from = c::bg000;
    float best = 0.0f;
    for (int ch = 0; ch < 3; ++ch)
    {
        const auto get = [ch] (juce::Colour x) { return ch == 0 ? x.getRed() : ch == 1 ? x.getGreen() : x.getBlue(); };
        const int span = (int) get (to) - (int) get (from);
        if (span != 0)
            best = juce::jmax (best, (float) ((int) get (p) - (int) get (from)) / (float) span);
    }
    return best;
}
} // namespace

TEST_CASE ("ground: the tile is 96px, opaque, and the window's own ground")
{
    const auto& tile = uv::ground::tile();
    REQUIRE (tile.getWidth() == uv::ground::tileSize);
    REQUIRE (tile.getHeight() == uv::ground::tileSize);

    double sum = 0.0;
    for (int y = 0; y < 96; ++y)
        for (int x = 0; x < 96; ++x)
        {
            const auto p = tile.getPixelAt (x, y);
            REQUIRE (p.getAlpha() == 255);
            sum += (double) towards (p, c::uvDeep);
        }

    /* "a 5 % uv-deep noise": away from the dots the grain averages 5 %. The
     * dots, 4 pixels in 144, move the mean a little. */
    const double mean = sum / (96.0 * 96.0);
    CHECK (mean > 0.03);
    CHECK (mean < 0.09);
}

TEST_CASE ("ground: the dots sit on the multiples of the pitch, 2 x 2 at 1x")
{
    const auto& tile = uv::ground::tile();
    /* Around (12, 12): the four pixels whose centres are within 1px. */
    for (const auto& p : { juce::Point<int> { 11, 11 }, { 12, 11 }, { 11, 12 }, { 12, 12 } })
        CHECK (towards (tile.getPixelAt (p.x, p.y), c::bgDot) > 0.9f);
    /* Between dots there is only grain. */
    CHECK (towards (tile.getPixelAt (17, 17), c::bgDot) < 0.5f);
    CHECK (towards (tile.getPixelAt (5, 5), c::bgDot) < 0.5f);
}

TEST_CASE ("ground: it repeats exactly, and lines up with the window's origin")
{
    juce::Image img (juce::Image::ARGB, 200, 120, true, juce::SoftwareImageType());
    {
        juce::Graphics g (img);
        uv::ground::paint (g, { 0.0f, 0.0f, 200.0f, 120.0f });
    }
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 96; ++x)
            REQUIRE (img.getPixelAt (x, y) == img.getPixelAt (x + 96, y));

    /* A part painted on its own, told where the window starts, matches. */
    juce::Image part (juce::Image::ARGB, 40, 40, true, juce::SoftwareImageType());
    {
        juce::Graphics g (part);
        uv::ground::paint (g, { 0.0f, 0.0f, 40.0f, 40.0f }, { -30.0f, -50.0f });
    }
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x)
            REQUIRE (part.getPixelAt (x, y) == img.getPixelAt (x + 30, y + 50));
}

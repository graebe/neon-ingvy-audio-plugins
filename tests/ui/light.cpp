// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The light: a glow falls off as CSS's blur does -- a Gaussian of half the
 * blur radius -- the focus ring stays outside its control, and nothing glows
 * past three deviations.
 */
#include "UvLight.h"
#include "UvTokens.h"

#include <doctest.h>

#include <cmath>

namespace c = uv::tok::colour;

namespace
{
/* The alpha CSS gives a blurred span [a, b] at `y`: the share of a Gaussian
 * of sigma = blur / 2, centred at y, that falls inside the span. */
float cssSpan (float y, float a, float b, float blur)
{
    const float k = 1.0f / (blur * 0.5f * std::sqrt (2.0f));
    return 0.5f * (std::erf ((b - y) * k) - std::erf ((a - y) * k));
}

juce::Image canvas (int w, int h)
{
    return juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
}
} // namespace

TEST_CASE ("light: a rectangle's shadow falls off as a CSS blur does")
{
    /* A long bar, so the profile across its middle is one edge's alone. */
    auto img = canvas (200, 100);
    const juce::Rectangle<float> bar (20.0f, 40.0f, 160.0f, 20.0f);
    const uv::tok::ShadowLayer layer { 0.0f, 0.0f, 10.0f, 0.0f, uv::tok::argb::ultraviolet::uvDeep };
    {
        juce::Graphics g (img);
        uv::light::shadow (g, bar, layer);
    }

    /* Down the middle column, through the bar and out past its bottom edge;
     * a pixel is sampled at its centre. Even the bar's middle is short of
     * full: 20px is only four deviations across. */
    for (int row = 30; row < 80; ++row)
    {
        CAPTURE (row);
        const float want = cssSpan ((float) row + 0.5f, 40.0f, 60.0f, 10.0f);
        const float got = img.getPixelAt (100, row).getFloatAlpha();
        CHECK (std::abs (got - want) <= 0.02f);   // two levels of 8-bit alpha
    }

    /* Past three deviations (15px) and a pixel of rounding, nothing. */
    CHECK (img.getPixelAt (100, 60 + 17).getAlpha() == 0);
}

TEST_CASE ("light: a drop-shadow's length is its deviation, twice a box-shadow's")
{
    /* The same 4px, read both ways, under the same long bar. */
    auto box = canvas (200, 100), drop = canvas (200, 100);
    juce::Path bar;
    bar.addRectangle (20.0f, 40.0f, 160.0f, 20.0f);
    const uv::tok::ShadowLayer layer { 0.0f, 0.0f, 4.0f, 0.0f, uv::tok::argb::ultraviolet::uvDeep };
    {
        juce::Graphics g (box);
        uv::light::shadow (g, bar, layer);
    }
    {
        juce::Graphics g (drop);
        uv::light::dropShadow (g, bar, layer);
    }

    for (int row = 60; row < 76; ++row)
    {
        CAPTURE (row);
        const float y = (float) row + 0.5f;
        /* Within SVG's three-box approximation of a Gaussian: "roughly 3 %". */
        CHECK (std::abs (box.getPixelAt (100, row).getFloatAlpha() - cssSpan (y, 40.0f, 60.0f, 4.0f)) <= 0.03f);
        CHECK (std::abs (drop.getPixelAt (100, row).getFloatAlpha() - cssSpan (y, 40.0f, 60.0f, 8.0f)) <= 0.03f);
    }
    CHECK (drop.getPixelAt (100, 66).getAlpha() > box.getPixelAt (100, 66).getAlpha());
}

TEST_CASE ("light: glow-led is the web kit's two drop-shadows, at its strength")
{
    auto img = canvas (120, 120);
    const juce::Rectangle<float> step (40.0f, 40.0f, 40.0f, 40.0f);
    {
        juce::Graphics g (img);
        uv::light::glowLed (g, step);
    }

    /* Just outside the edge both halos show; further out the violet one is
     * most of what is left, and the whole never exceeds ledOpacity's worth
     * of both. */
    const auto near = img.getPixelAt (60, 81);
    const auto far = img.getPixelAt (60, 87);
    CHECK (near.getFloatAlpha() > far.getFloatAlpha());
    CHECK (near.getFloatAlpha() < 1.0f - (1.0f - uv::light::ledOpacity) * (1.0f - uv::light::ledOpacity) + 0.02f);
    CHECK (far.getBlue() > far.getGreen());   // uv-deep's hue: violet, not white
    /* A 10px deviation reaches about 30px; past that, nothing. */
    CHECK (img.getPixelAt (60, 80 + 20).getAlpha() > 0);
    CHECK (img.getPixelAt (60, 119).getAlpha() == 0);

    /* The same glow for the same size wherever the step is: kept, not
     * recomputed per step. */
    auto moved = canvas (120, 120);
    {
        juce::Graphics g (moved);
        uv::light::glowLed (g, step.translated (-20.0f, -20.0f));
    }
    CHECK (moved.getPixelAt (40, 61) == img.getPixelAt (60, 81));
}

TEST_CASE ("light: glow-focus rings its control and never paints inside it")
{
    auto img = canvas (80, 60);
    const juce::Rectangle<float> button (20.0f, 16.0f, 40.0f, 28.0f);
    {
        juce::Graphics g (img);
        uv::light::glowFocus (g, button);
    }

    for (int y = 16; y < 44; ++y)
        for (int x = 20; x < 60; ++x)
            REQUIRE (img.getPixelAt (x, y).getAlpha() == 0);

    /* The 1px ring just outside, in uv. */
    const auto ring = img.getPixelAt (40, 15);
    CHECK (ring.getAlpha() == 255);
    CHECK (ring.getRed() == c::uv.getRed());
    /* And the halo past it, fading. */
    CHECK (img.getPixelAt (40, 12).getAlpha() > 0);
    CHECK (img.getPixelAt (40, 12).getAlpha() < 128);
}

TEST_CASE ("light: a round control's focus follows its outline")
{
    auto img = canvas (64, 64);
    const juce::Rectangle<float> disc (16.0f, 16.0f, 32.0f, 32.0f);
    {
        juce::Graphics g (img);
        uv::light::glowFocus (g, disc, 16.0f);
    }
    CHECK (img.getPixelAt (32, 32).getAlpha() == 0);   // the disc's middle
    CHECK (img.getPixelAt (32, 15).getAlpha() > 128);  // its ring, at the top
    CHECK (img.getPixelAt (19, 19).getAlpha() < 160);  // the corner a square ring would fill
}

TEST_CASE ("light: the arc glow is soft and short")
{
    auto img = canvas (64, 32);
    juce::Path line;
    line.addRectangle (8.0f, 15.0f, 48.0f, 2.0f);
    {
        juce::Graphics g (img);
        uv::light::glowArc (g, line);
    }
    CHECK (img.getPixelAt (32, 15).getFloatAlpha() <= uv::light::arcOpacity + 0.02f);
    CHECK (img.getPixelAt (32, 18).getAlpha() > 0);
    CHECK (img.getPixelAt (32, 25).getAlpha() == 0);   // 3px blur: gone by 8px
}

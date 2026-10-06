// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Light past a component's edge: its parent paints it, over its own
 * background and under the child, where the child alone could not reach.
 */
#include "Luminous.h"
#include "UvTokens.h"
#include "snapshot.h"

#include <doctest.h>

namespace c = uv::tok::colour;

namespace
{
struct Lit final : public juce::Component, public ni::ui::Luminous
{
    void paintLight (juce::Graphics& g) override
    {
        /* A ring 4px outside the bounds: light only the parent can draw. */
        g.setColour (c::uvDeep);
        g.drawRect (getLocalBounds().expanded (4), 1);
    }
    void paint (juce::Graphics& g) override { g.fillAll (c::uv); }
};

struct Container final : public juce::Component
{
    Container()
    {
        setSize (100, 60);
        addAndMakeVisible (lit);
        lit.setBounds (30, 20, 40, 20);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (c::bg100);
        ni::ui::paintChildLights (g, *this);
    }
    Lit lit;
};
} // namespace

TEST_CASE ("luminous: a child's light is painted by its parent, past the child's edge")
{
    Container box;
    const auto img = ni::ui::test::render (box);

    CHECK (img.getPixelAt (50, 16) == c::uvDeep);   // the ring above the child: outside its bounds
    CHECK (img.getPixelAt (50, 30) == c::uv);       // the child over its own light
    CHECK (img.getPixelAt (5, 5) == c::bg100);      // the parent's ground elsewhere
}

TEST_CASE ("luminous: an invisible child has no light")
{
    Container box;
    box.lit.setVisible (false);
    const auto img = ni::ui::test::render (box);
    CHECK (img.getPixelAt (50, 16) == c::bg100);
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Meter: a flat fill from the left that jumps to its level, uv with its
 * glow when live and line-200 without when off, the glow reaching past the
 * bed -- and to assistive technology, a meter from 0 to 100.
 */
#include "Meter.h"

#include "UvTokens.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

#include <cmath>

using ni::ui::Meter;
namespace c = uv::tok::colour;

namespace
{
/* A row that paints the ground and its children's light, as a panel does. */
struct Row final : public juce::Component
{
    Row()
    {
        setSize (200, 32);
        addAndMakeVisible (meter);
        meter.setBounds (20, 10, 160, Meter::height);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (c::bg000);
        ni::ui::paintChildLights (g, *this);
    }
    Meter meter;
};
} // namespace

TEST_CASE ("meter: the card's bed, and nothing to press or focus")
{
    Meter m;
    CHECK (m.getHeight() == 12);
    CHECK (m.getWidth() == 64);

    bool self = true, children = true;
    m.getInterceptsMouseClicks (self, children);
    CHECK_FALSE (self);
    CHECK_FALSE (m.getWantsKeyboardFocus());
}

TEST_CASE ("meter: the level is clamped, and the fill is that share of the bed's inside")
{
    Meter m;
    m.setSize (162, 12);
    m.setLevel (0.5f);
    CHECK (m.fillBounds() == juce::Rectangle<float> (1.0f, 1.0f, 80.0f, 10.0f));

    m.setLevel (2.0f);
    CHECK (m.getLevel() == 1.0f);
    CHECK (m.fillBounds().getWidth() == 160.0f);

    m.setLevel (-1.0f);
    CHECK (m.getLevel() == 0.0f);
    CHECK (m.fillBounds().isEmpty());

    m.setLevel (std::nanf (""));
    CHECK (m.getLevel() == 0.0f);
}

TEST_CASE ("meter: live is uv with violet light past the bed; off is line-200 and dark")
{
    Row row;
    row.meter.setLevel (0.5f);
    auto img = ni::ui::test::render (row);

    /* Inside the fill: uv. Past its end, on the bed: bg-200 lit by the glow. */
    CHECK (img.getPixelAt (40, 16) == c::uv);
    const auto bedNearFill = img.getPixelAt (102, 16);
    const auto bedFarAway = img.getPixelAt (170, 16);
    CHECK (bedNearFill.getBlue() > bedFarAway.getBlue());
    CHECK (bedFarAway == c::bg200);

    /* Above the meter, outside it: the light only the parent can paint. */
    const auto above = img.getPixelAt (40, 8);
    CHECK (above.getBlue() > c::bg000.getBlue() + 10);

    row.meter.setLive (false);
    img = ni::ui::test::render (row);
    CHECK (img.getPixelAt (40, 16) == c::line200);
    CHECK (img.getPixelAt (40, 8) == c::bg000);
    CHECK (img.getPixelAt (102, 16) == c::bg200);
}

TEST_CASE ("meter: empty draws no fill and no light")
{
    Row row;
    row.meter.setLevel (0.0f);
    const auto img = ni::ui::test::render (row);
    CHECK (img.getPixelAt (22, 16) == c::bg200);
    CHECK (img.getPixelAt (22, 8) == c::bg000);
}

TEST_CASE ("meter: assistive technology reads a meter from 0 to 100")
{
    Meter m;
    m.setTitle ("Input level");
    m.setLevel (0.62f);

    /* getAccessibilityHandler() answers only on screen; this is what it makes. */
    const auto handler = m.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::progressBar);
    CHECK (handler->getTitle() == "Input level");

    auto* value = handler->getValueInterface();
    REQUIRE (value != nullptr);
    CHECK (value->isReadOnly());
    CHECK (value->getCurrentValue() == 62.0);
    CHECK (value->getRange().getMinimumValue() == 0.0);
    CHECK (value->getRange().getMaximumValue() == 100.0);
}

NI_SNAPSHOT_TEST ("meter: live, quiet, off, empty and full")
{
    NI_CHECK_PAGE ("display-meter", "meter-states");
}

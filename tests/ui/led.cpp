// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The LED: a lens that says a reported state in the card's four colours, its
 * label beside it, the pointer for its info line and nothing to press or
 * focus -- and its light reaching past the row only when it is lit.
 */
#include "Led.h"

#include "UvTokens.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Led;
namespace c = uv::tok::colour;

namespace
{
/* A row that paints the ground and its children's light, as a panel does. */
struct Row final : public juce::Component
{
    Row()
    {
        setSize (160, 48);
        addAndMakeVisible (led);
        led.setBounds (20, 10, led.idealWidth(), 28);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (c::bg000);
        ni::ui::paintChildLights (g, *this);
    }
    Led led { "Listening" };
};

juce::Colour lensCentre (Row& row)
{
    const auto img = ni::ui::test::render (row);
    const auto centre = row.led.lens().getCentre() + row.led.getPosition().toFloat();
    return img.getPixelAt ((int) centre.x, (int) centre.y);
}
} // namespace

TEST_CASE ("led: the card's lens and label on a control-h row; the pointer, never a press or the keyboard")
{
    Led led ("Listening");
    CHECK (led.getHeight() == 28);
    CHECK (led.lens() == juce::Rectangle<float> (0.0f, 10.0f, 8.0f, 8.0f));
    CHECK (led.idealWidth() > 8 + 8);
    CHECK (led.getWidth() == led.idealWidth());
    CHECK (Led::idealWidthFor ("Listening") == led.idealWidth());
    CHECK (Led::idealWidthFor ("Unavailable") > led.idealWidth());
    CHECK (Led::idealWidthFor ({}) == 8);

    bool self = false, children = true;
    led.getInterceptsMouseClicks (self, children);
    CHECK (self);
    CHECK_FALSE (children);
    CHECK_FALSE (led.getWantsKeyboardFocus());
    CHECK (led.getTitle() == "Listening");

    led.setLabel ("Slot taken");
    CHECK (led.getTitle() == "Slot taken");
}

TEST_CASE ("led: off is ink-dim, on uv, warn amber, clip red")
{
    Row row;
    CHECK (row.led.getStatus() == Led::Status::off);
    CHECK (lensCentre (row) == c::inkDim);

    row.led.setStatus (Led::Status::on);
    CHECK (lensCentre (row) == c::uv);
    row.led.setStatus (Led::Status::warn);
    CHECK (lensCentre (row) == c::amber);
    row.led.setStatus (Led::Status::clip);
    CHECK (lensCentre (row) == c::red);
}

TEST_CASE ("led: lit, its halo reaches past the row; off, it is dark")
{
    Row row;
    /* Left of the LED, outside its bounds: only the parent paints there. */
    const juce::Point<int> outside { 17, 24 };

    auto img = ni::ui::test::render (row);
    CHECK (img.getPixelAt (outside.x, outside.y) == c::bg000);

    row.led.setStatus (Led::Status::on);
    img = ni::ui::test::render (row);
    CHECK (img.getPixelAt (outside.x, outside.y).getBlue() > c::bg000.getBlue() + 10);

    row.led.setStatus (Led::Status::warn);
    img = ni::ui::test::render (row);
    CHECK (img.getPixelAt (outside.x, outside.y).getRed() > c::bg000.getRed() + 10);
}

TEST_CASE ("led: a screen reader reads its label")
{
    Led led ("Slot taken");
    const auto handler = led.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::staticText);
    CHECK (handler->getTitle() == "Slot taken");
}

NI_SNAPSHOT_TEST ("led: off, on, warn and clip")
{
    NI_CHECK_PAGE ("display-led", "led-states");
}

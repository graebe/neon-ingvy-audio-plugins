// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The publisher's signature: the card's geometry, its light past its edge,
 * what assistive technology hears, and its picture.
 */
#include "Signature.h"

#include "Info.h"
#include "UvTokens.h"
#include "UvType.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Signature;
namespace c = uv::tok::colour;

namespace
{
/* A container that paints the ground and its children's light, as the Hint
 * does. */
struct Bar final : public juce::Component
{
    Bar()
    {
        setSize (160, 40);
        addAndMakeVisible (sig);
        sig.setTopLeftPosition (40, 13);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (c::bg000);
        ni::ui::paintChildLights (g, *this);
    }
    Signature sig;
};

int violet (juce::Colour p)
{
    /* How far a pixel has moved from bg-000 towards the light's violet. */
    return (int) p.getBlue() - (int) c::bg000.getBlue();
}
} // namespace

TEST_CASE ("signature: the card's geometry, sized by itself")
{
    Signature sig;
    CHECK (sig.getHeight() == 14);

    /* The mark, 8px of gap, then NEON INGVY in the card's tracked capitals. */
    const float words = uv::type::width (uv::type::font (Signature::style), "NEON INGVY");
    CHECK (sig.getWidth() == (int) std::ceil (6.0f + 8.0f + words));
    /* JetBrains Mono's 0.6em advance and 0.2em of tracking, ten letters. */
    CHECK (words == doctest::Approx (80.0f).epsilon (0.02));

    CHECK (sig.markBounds() == juce::Rectangle<float> (0.0f, 4.0f, 6.0f, 6.0f));
}

TEST_CASE ("signature: the mark is lit in uv, and its halo falls outside the signature")
{
    Bar bar;
    const auto img = ni::ui::test::render (bar);

    /* The mark: uv, near-white. */
    CHECK (img.getPixelAt (43, 20) == c::uv);

    /* Left of the component, where only its parent can paint: violet light. */
    CHECK (violet (img.getPixelAt (36, 20)) > 6);
    /* Far from it, the ground as it was. */
    CHECK (img.getPixelAt (2, 2) == c::bg000);
}

TEST_CASE ("signature: assistive technology hears the name and the editor's line")
{
    Signature sig;
    /* getAccessibilityHandler() answers only on screen; this is what it makes. */
    const auto handler = sig.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::staticText);
    CHECK (handler->getTitle() == "Neon Ingvy");

    const auto line = juce::String::fromUTF8 ("Neon Ingvy \xe2\x80\x94 the publisher of this plugin.");
    ni::ui::setInfo (sig, line);
    CHECK (sig.getDescription() == line);
    CHECK (ni::ui::infoOf (sig) == line);
}

NI_SNAPSHOT_TEST ("signature: at its size, and twice it for inspection")
{
    NI_CHECK_PAGE ("display-signature", "signature");
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The editable Ring: its band from the card, a wedge per step from 12
 * o'clock, a hit band 6px past either edge and a dead hole, a press then a
 * sweep that paints each wedge once, the keyboard as its count with the
 * detents, and a slider to a screen reader. Then the ring, as a picture.
 */
#include "Gallery.h"
#include "Pointer.h"
#include "Ring.h"
#include "Step.h"
#include "UvTokens.h"
#include "checks.h"
#include "snapshot.h"

#include <doctest.h>

#include <cmath>

using ni::ui::Ring;
using ni::ui::gallery::Pointer;
using ni::ui::gallery::key;

namespace
{
/* The point at `radius` from the centre, `turns` of a circle clockwise from
 * 12 o'clock. */
juce::Point<float> at (float radius, float turns)
{
    const float a = turns * juce::MathConstants<float>::twoPi;
    return { 120.0f + std::sin (a) * radius, 120.0f - std::cos (a) * radius };
}

struct Rig
{
    Rig()
    {
        ring.setCount (16);
        ring.onPress = [this] (int i, bool shift)
        {
            log.add ("press " + juce::String (i) + (shift ? " shift" : ""));
            return allowSweep;
        };
        ring.onSweep = [this] (int i) { log.add ("sweep " + juce::String (i)); };
        ring.onCount = [this] (int n) { log.add ("count " + juce::String (n)); };
    }

    juce::String said() const { return log.joinIntoString (", "); }

    Ring ring;
    juce::StringArray log;
    bool allowSweep = true;
};
} // namespace

TEST_CASE ("ring: the band is 34px at 16 steps, narrows from the inside, never under 24")
{
    auto b = Ring::bandFor (240.0f, 16);
    CHECK (b.outer == doctest::Approx (114.0f));
    CHECK (b.outer - b.inner == doctest::Approx (34.0f));
    b = Ring::bandFor (240.0f, 32);
    CHECK (b.outer - b.inner == doctest::Approx (28.125f));
    b = Ring::bandFor (240.0f, 128);
    CHECK (b.outer == doctest::Approx (114.0f));
    CHECK (b.outer - b.inner == doctest::Approx (24.0f));
    /* Fractions of the size, so the drawing works at any. */
    CHECK (Ring::bandFor (120.0f, 16).outer == doctest::Approx (57.0f));
}

TEST_CASE ("ring: a wedge per step from 12 o'clock, a hit band 6px past the band, a dead hole")
{
    Rig rig;
    const auto b = rig.ring.band();
    CHECK (rig.ring.stepAt (at (100.0f, 0.01f)) == 0);
    CHECK (rig.ring.stepAt (at (100.0f, 0.99f)) == 15);
    CHECK (rig.ring.stepAt (at (100.0f, 0.25f + 0.01f)) == 4);   // 3 o'clock begins step 4
    CHECK (rig.ring.stepAt (at (b.outer + 5.0f, 0.5f + 0.01f)) == 8);
    CHECK (rig.ring.stepAt (at (b.inner - 5.0f, 0.5f + 0.01f)) == 8);
    CHECK (rig.ring.stepAt (at (b.inner - 7.0f, 0.5f)) == -1);
    CHECK (rig.ring.stepAt ({ 120.0f, 120.0f }) == -1);

    Pointer p;
    p.click (rig.ring, { 120.0f, 120.0f + 30.0f });
    CHECK (rig.said().isEmpty());
}

TEST_CASE ("ring: a press, then a sweep that paints every wedge it crosses once")
{
    Rig rig;
    Pointer p;
    p.down (rig.ring, at (100.0f, 0.5f / 16.0f), juce::ModifierKeys::shiftModifier);
    p.drag (at (100.0f, 1.5f / 16.0f));
    p.drag (at (100.0f, 1.6f / 16.0f));     // the same wedge: once
    p.drag (at (100.0f, 2.5f / 16.0f));
    p.drag (at (100.0f, 0.5f / 16.0f));     // back over the pressed one: never
    p.drag (at (40.0f, 3.5f / 16.0f));      // through the hole: nothing
    p.up();
    CHECK (rig.said() == "press 0 shift, sweep 1, sweep 2");

    rig.log.clear();
    rig.allowSweep = false;
    p.down (rig.ring, at (100.0f, 4.5f / 16.0f));
    p.drag (at (100.0f, 5.5f / 16.0f));
    p.up();
    CHECK (rig.said() == "press 4");
}

TEST_CASE ("ring: to the keyboard it is its count -- arrows, the detents by Page, the ends")
{
    Rig rig;
    rig.ring.setRange (1, 128);
    rig.ring.setDetents ({ 8.0, 16.0, 32.0, 64.0 });
    CHECK (key (rig.ring, juce::KeyPress::upKey));
    CHECK (key (rig.ring, juce::KeyPress::leftKey));
    CHECK (key (rig.ring, juce::KeyPress::pageUpKey));
    CHECK (key (rig.ring, juce::KeyPress::pageDownKey));
    CHECK (key (rig.ring, juce::KeyPress::endKey));
    CHECK (key (rig.ring, juce::KeyPress::homeKey));
    CHECK (rig.said() == "count 17, count 15, count 32, count 8, count 128, count 1");

    rig.ring.setDetents ({});
    rig.log.clear();
    CHECK (key (rig.ring, juce::KeyPress::pageUpKey));
    CHECK (rig.said() == "count 20");
}

TEST_CASE ("ring: a slider named Length, its value read as the owner writes it")
{
    Rig rig;
    rig.ring.valueText = [] (int n) { return juce::String (n) + " steps"; };
    /* getAccessibilityHandler() answers only on screen; this is what it makes. */
    const auto handler = static_cast<juce::Component&> (rig.ring).createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::slider);
    CHECK (handler->getTitle() == "Length");
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "16 steps");
    handler->getValueInterface()->setValue (24.0);
    CHECK (rig.said() == "count 24");
}

TEST_CASE ("ring: a step not heard yet is hollow, a hole still heard is filled at the Step's alpha")
{
    namespace c = uv::tok::colour;
    Rig rig;
    Ring::StepState pending;
    pending.pending = true;
    Ring::StepState filled;
    filled.filled = true;
    filled.amount = 0.5f;
    rig.ring.setStep (2, pending);
    rig.ring.setStep (4, filled);
    rig.ring.setStep (6, filled);
    rig.ring.setStep (6, {});   // a gap: the rail alone

    const auto b = rig.ring.band();
    const float mid = (b.inner + b.outer) * 0.5f;
    const auto pixel = [&] (const juce::Image& img, float radius, int step)
    {
        const auto p = at (radius, ((float) step + 0.5f) / 16.0f);
        return img.getPixelAt ((int) p.x, (int) p.y);
    };
    const auto img = ni::ui::test::render (rig.ring);

    /* Pending: the rail inside a uv outline, and nothing lit. */
    CHECK (pixel (img, mid, 2) == c::line200);
    CHECK (pixel (img, b.outer - 0.5f, 2).getBrightness() > c::line200.getBrightness() + 0.2f);
    /* Filled: uv at 0.55 over the rail up to its level, the rail past it;
     * no outline at the outer edge. */
    const auto lit = c::line200.overlaidWith (c::uv.withMultipliedAlpha (ni::ui::Step::filledAlpha));
    CHECK (pixel (img, b.inner + (b.outer - b.inner) * 0.25f, 4).getBrightness()
           == doctest::Approx (lit.getBrightness()).epsilon (0.02));
    CHECK (pixel (img, b.inner + (b.outer - b.inner) * 0.75f, 4) == c::line200);
    CHECK (pixel (img, b.outer - 0.5f, 4).getBrightness()
           == doctest::Approx (pixel (img, b.outer - 0.5f, 6).getBrightness()).epsilon (0.02));
    /* A gap is the rail. */
    CHECK (pixel (img, mid, 6) == c::line200);
}

NI_SNAPSHOT_TEST ("ring: on, partial, tie, cursor, playhead; 128 steps with focus")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-ring")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-ring");
}

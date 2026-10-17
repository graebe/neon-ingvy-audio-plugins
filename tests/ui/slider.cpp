// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The slider: a press jumps the value there and a drag follows, Shift drags
 * finer from where it was, the keys step, a double-click resets, and it is
 * a slider to a screen reader. Then every state, as a picture.
 */
#include "Pointer.h"
#include "Slider.h"
#include "checks.h"
#include "hostkeys.h"
#include "pages.h"

#include <doctest.h>

using ni::ui::Slider;
using ni::ui::gallery::Pointer;

namespace
{
/* A slider and a model that takes everything it is asked. */
struct Model
{
    Model()
    {
        slider.setBounds (0, 0, 120, 24);
        slider.onBegin = [this] { ++begun; };
        slider.onInput = [this] (float v) { take (v); };
        slider.onEnd = [this] { ++ended; };
        slider.onCommit = [this] (float v) { take (v); ++commits; };
        slider.onReset = [this] { take (0.5f); };
    }
    void take (float v)
    {
        value = v;
        slider.setValue (v);
    }
    Slider slider;
    float value = 0.0f;
    int begun = 0, ended = 0, commits = 0;
};
} // namespace

TEST_CASE ("slider: a press jumps to the pointer, a drag follows, one gesture")
{
    Model m;
    const auto rail = m.slider.rail();
    Pointer p;
    p.down (m.slider, { rail.getX() + rail.getWidth() * 0.25f, 12.0f });
    CHECK (m.value == doctest::Approx (0.25f));
    CHECK (m.begun == 1);
    p.drag ({ rail.getX() + rail.getWidth() * 0.75f, 12.0f });
    CHECK (m.value == doctest::Approx (0.75f));
    p.drag ({ rail.getRight() + 50.0f, 12.0f });
    CHECK (m.value == doctest::Approx (1.0f));
    p.up();
    CHECK (m.ended == 1);
    CHECK (m.slider.getValue() == doctest::Approx (1.0f));
    CHECK (m.slider.thumb().getCentreX() == doctest::Approx (rail.getRight()));
}

TEST_CASE ("slider: Shift drags five times finer, from where it was")
{
    Model m;
    m.take (0.5f);
    const auto rail = m.slider.rail();
    Pointer p;
    p.down (m.slider, { rail.getX() + 1.0f, 12.0f }, juce::ModifierKeys::shiftModifier);
    CHECK (m.value == doctest::Approx (0.5f));
    CHECK (m.begun == 0);
    p.drag ({ rail.getX() + 1.0f + rail.getWidth() * 0.5f, 12.0f });
    CHECK (m.value == doctest::Approx (0.6f));
    p.up();
    CHECK ((m.begun == 1 && m.ended == 1));
}

TEST_CASE ("slider: keys step and each is one edit; a double-click resets; Space is the host's")
{
    Model m;
    ni::ui::gallery::key (m.slider, juce::KeyPress::rightKey);
    CHECK (m.value == doctest::Approx (0.01f));
    ni::ui::gallery::key (m.slider, juce::KeyPress::endKey);
    CHECK (m.value == doctest::Approx (1.0f));
    CHECK (m.commits == 2);
    CHECK_FALSE (ni::ui::test::windowUses (m.slider, ni::ui::test::spaceKey));
    Pointer p;
    p.doubleClick (m.slider, { 30.0f, 12.0f });
    CHECK (m.value == doctest::Approx (0.5f));
    m.slider.setEnabled (false);
    CHECK_FALSE (ni::ui::gallery::key (m.slider, juce::KeyPress::leftKey));
}

TEST_CASE ("slider: vertical runs bottom to top; a screen reader moves it")
{
    Slider s (Slider::Orientation::vertical);
    s.setBounds (0, 0, 24, 120);
    CHECK (s.valueAt ({ 12.0f, s.rail().getBottom() }) == doctest::Approx (0.0f));
    CHECK (s.valueAt ({ 12.0f, s.rail().getY() }) == doctest::Approx (1.0f));
    s.setValue (0.5f);
    CHECK (s.thumb().getCentreY() == doctest::Approx (s.rail().getCentreY()));
    float asked = -1.0f;
    s.onCommit = [&] (float v) { asked = v; };
    s.setValueText ("50 %");
    auto handler = s.createAccessibilityHandler();
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "50 %");
    handler->getValueInterface()->setValue (0.3);
    CHECK (asked == doctest::Approx (0.3f));
    CHECK (handler->getRole() == juce::AccessibilityRole::slider);
}

NI_SNAPSHOT_TEST ("slider: every state")
{
    NI_CHECK_PAGE ("Slider", "controls-slider");
}

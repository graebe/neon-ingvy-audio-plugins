// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The keyboard, for every control that takes a pointer: the web kit's
 * keys.test.mjs, case for case, against the native map.
 */
#include "Keys.h"

#include <doctest.h>

using namespace ni::ui::keys;

namespace
{
juce::KeyPress press (int code, int mods = 0)
{
    return juce::KeyPress (code, juce::ModifierKeys (mods), 0);
}

constexpr int shift = juce::ModifierKeys::shiftModifier;
constexpr int alt = juce::ModifierKeys::altModifier;
} // namespace

TEST_CASE ("keys: arrows move through a grid of 16 to a row and stop at its edges")
{
    CHECK (gridMove (Key::right, 3, 32, 16) == 4);
    CHECK (gridMove (Key::right, 31, 32, 16) == 31);
    CHECK (gridMove (Key::left, 0, 32, 16) == 0);
    CHECK (gridMove (Key::down, 3, 32, 16) == 19);
    CHECK (gridMove (Key::down, 19, 32, 16) == 19);
    CHECK (gridMove (Key::up, 19, 32, 16) == 3);
    CHECK (gridMove (Key::down, 3, 16, 16) == 3);    // one row: nowhere to go
    CHECK (gridMove (Key::home, 21, 32, 16) == 16);
    CHECK (gridMove (Key::end, 17, 20, 16) == 19);   // a short last row ends early
    CHECK_FALSE (gridMove (Key::space, 3, 32, 16).has_value());
}

TEST_CASE ("keys: a pad toggles on Space and Enter, ties with shift, and alt-arrows its amount")
{
    auto a = padKey (press (juce::KeyPress::spaceKey), 0, 16, 16);
    CHECK (a.kind == PadAction::Kind::toggle);
    CHECK_FALSE (a.tie);

    a = padKey (press (juce::KeyPress::returnKey, shift), 0, 16, 16);
    CHECK (a.kind == PadAction::Kind::toggle);
    CHECK (a.tie);

    a = padKey (press (juce::KeyPress::upKey, alt), 0, 16, 16);
    CHECK (a.kind == PadAction::Kind::depth);
    CHECK (a.depth == doctest::Approx (0.1f));

    a = padKey (press (juce::KeyPress::downKey, alt | shift), 0, 16, 16);
    CHECK (a.kind == PadAction::Kind::depth);
    CHECK (a.depth == doctest::Approx (-0.01f));

    a = padKey (press (juce::KeyPress::rightKey), 0, 16, 16);
    CHECK (a.kind == PadAction::Kind::move);
    CHECK (a.to == 1);

    CHECK (padKey (press ('a'), 0, 16, 16).kind == PadAction::Kind::none);
}

TEST_CASE ("keys: a slider answers the knob's keys, limited to its axis")
{
    auto a = sliderKey (press (juce::KeyPress::rightKey), Axis::x);
    CHECK (a.kind == SliderAction::Kind::delta);
    CHECK (a.amount == doctest::Approx (0.01f));

    a = sliderKey (press (juce::KeyPress::leftKey, shift), Axis::x);
    CHECK (a.amount == doctest::Approx (-0.002f));

    CHECK (sliderKey (press (juce::KeyPress::upKey), Axis::x).kind == SliderAction::Kind::none);
    CHECK (sliderKey (press (juce::KeyPress::upKey), Axis::y).amount == doctest::Approx (0.01f));
    CHECK (sliderKey (press (juce::KeyPress::pageDownKey)).amount == doctest::Approx (-0.1f));

    a = sliderKey (press (juce::KeyPress::endKey));
    CHECK (a.kind == SliderAction::Kind::to);
    CHECK (a.amount == 1.0f);

    CHECK (sliderKey (press (juce::KeyPress::tabKey)).kind == SliderAction::Kind::none);
}

TEST_CASE ("keys: tabs move with the arrows and wrap")
{
    CHECK (tabMove (Key::down, 1, 2) == 0);
    CHECK (tabMove (Key::up, 0, 2) == 1);
    CHECK (tabMove (Key::home, 1, 2) == 0);
    CHECK_FALSE (tabMove (Key::enter, 1, 2).has_value());
}

TEST_CASE ("keys: a count steps by one, pages by four, and stops at its ends")
{
    CHECK (countKey (press (juce::KeyPress::upKey), 16, 1, 128) == 17);
    CHECK (countKey (press (juce::KeyPress::leftKey), 1, 1, 128) == 1);
    CHECK (countKey (press (juce::KeyPress::pageUpKey), 126, 1, 128) == 128);
    CHECK (countKey (press (juce::KeyPress::homeKey), 16, 1, 128) == 1);
    CHECK_FALSE (countKey (press (juce::KeyPress::spaceKey), 16, 1, 128).has_value());
}

TEST_CASE ("keys: a count pages between detents, and by its page where there is none")
{
    const std::vector<double> at { 16, 32, 64, 128 };
    CHECK (countKey (press (juce::KeyPress::pageUpKey), 20, 1, 128, 4, at) == 32);
    CHECK (countKey (press (juce::KeyPress::pageDownKey), 20, 1, 128, 4, at) == 16);
    CHECK (countKey (press (juce::KeyPress::pageUpKey), 128, 1, 128, 4, at) == 128);
    CHECK (countKey (press (juce::KeyPress::pageDownKey), 10, 1, 128, 4, at) == 6);  // none below: the page
    CHECK (countKey (press (juce::KeyPress::pageUpKey), 20, 1, 128, 4) == 24);        // no detents: the page
    CHECK (countKey (press (juce::KeyPress::upKey), 20, 1, 128, 4, at) == 21);       // arrows stay one
    CHECK (countKey (press (juce::KeyPress::downKey), 32, 1, 128, 4, at) == 31);
}

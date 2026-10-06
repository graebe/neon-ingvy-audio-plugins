// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The switch: it asks for the other state and shows what it is told; its
 * housing rises under the pointer, not its label; and it is a toggle to a
 * screen reader. Then every state, as a picture.
 */
#include "Gallery.h"
#include "Pointer.h"
#include "Toggle.h"
#include "checks.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Toggle;
using ni::ui::gallery::Pointer;

namespace
{
/* A switch whose model is a bool, as ParamToggle's is the parameter. */
struct Switched
{
    Switched()
    {
        toggle.setBounds (0, 0, toggle.idealWidth(), 28);
        toggle.onChange = [this] (bool on)
        {
            asked.push_back (on);
            model = on;
            toggle.setOn (model);
        };
    }
    Toggle toggle { "Soft" };
    bool model = false;
    std::vector<bool> asked;
};
} // namespace

TEST_CASE ("toggle: a click asks for the other state, and shows what the model holds")
{
    Switched s;
    Pointer p;

    p.click (s.toggle, { 40, 14 });   // on the label: the whole row is the switch
    CHECK (s.asked == std::vector<bool> { true });
    CHECK (s.toggle.isOn());

    p.click (s.toggle, { 10, 14 });
    CHECK (s.asked == std::vector<bool> { true, false });
    CHECK_FALSE (s.toggle.isOn());
}

TEST_CASE ("toggle: it does not flip itself -- a model that refuses keeps it where it was")
{
    Toggle t ("Soft");
    t.setBounds (0, 0, 80, 28);
    int asked = 0;
    t.onChange = [&] (bool) { ++asked; };

    Pointer p;
    p.click (t, { 10, 14 });
    CHECK (asked == 1);
    CHECK_FALSE (t.isOn());
}

TEST_CASE ("toggle: Space and Enter flip it from the keyboard; disabled, nothing does")
{
    Switched s;
    ni::ui::gallery::space (s.toggle);
    CHECK (s.toggle.isOn());
    ni::ui::gallery::key (s.toggle, juce::KeyPress::returnKey);
    CHECK_FALSE (s.toggle.isOn());

    s.toggle.setEnabled (false);
    ni::ui::gallery::space (s.toggle);
    Pointer p;
    p.click (s.toggle, { 10, 14 });
    CHECK (s.asked.size() == 2);
}

TEST_CASE ("toggle: the housing rises under the pointer, the label does not")
{
    Toggle t ("Join Neighbors");
    t.setBounds (0, 0, t.idealWidth(), 28);
    Pointer p;

    p.enter (t, { 40, 14 });            // over the label
    CHECK (t.isHovered());
    CHECK_FALSE (t.isHousingHovered());
    p.move (t, { 10, 14 });             // onto the housing
    CHECK (t.isHousingHovered());
    p.exit (t);
    CHECK_FALSE (t.isHousingHovered());
}

TEST_CASE ("toggle: a 28 x 14 housing, the square 2px in and at 16px when on, the label 8px after")
{
    Toggle t ("Soft");
    t.setBounds (0, 0, t.idealWidth(), 28);

    CHECK (t.housing() == juce::Rectangle<float> (0, 7, 28, 14));
    CHECK (t.square() == juce::Rectangle<float> (3, 10, 8, 8));
    t.setOn (true);
    CHECK (t.square() == juce::Rectangle<float> (17, 10, 8, 8));

    Toggle bare;
    CHECK (bare.idealWidth() == 28);
    CHECK (t.idealWidth() > 28 + 8);
}

TEST_CASE ("toggle: a screen reader hears a toggle, its label and its state")
{
    Toggle t ("Soft");
    auto handler = static_cast<juce::Component&> (t).createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::toggleButton);
    CHECK (handler->getTitle() == "Soft");
    CHECK (handler->getCurrentState().isCheckable());
    CHECK_FALSE (handler->getCurrentState().isChecked());
    t.setOn (true);
    CHECK (handler->getCurrentState().isChecked());

    bool asked = false;
    t.onChange = [&] (bool on) { asked = ! on; };
    CHECK (handler->getActions().invoke (juce::AccessibilityActionType::toggle));
    CHECK (asked);
}

NI_SNAPSHOT_TEST ("toggle: every state of the switch")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-toggle")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-toggle");
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Tabs: equal shares of the strip, a click or a key asks for one, the arrows
 * wrap, one Tab stop -- the lit tab -- and a radio button to a screen reader
 * that knows which is lit. Then the strips, as a picture.
 */
#include "Gallery.h"
#include "Pointer.h"
#include "Tabs.h"
#include "checks.h"
#include "hostkeys.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Tabs;
using ni::ui::gallery::Pointer;
using ni::ui::gallery::key;

namespace
{
/* A strip whose owner lights what it is asked for, as the Band does. */
struct Strip
{
    explicit Strip (const juce::StringArray& names = { "Pattern", "Signal" }, int height = 92)
    {
        tabs.setTabs (names);
        tabs.setBounds (0, 0, Tabs::width, height);
        tabs.onSelect = [this] (int i)
        {
            asked.push_back (i);
            tabs.setActive (i);
        };
    }
    Tabs tabs;
    std::vector<int> asked;
};
} // namespace

TEST_CASE ("tabs: each tab is an equal share of the strip, filling it exactly")
{
    Strip two;
    CHECK (two.tabs.getTab (0).getBounds() == juce::Rectangle<int> (0, 0, 24, 46));
    CHECK (two.tabs.getTab (1).getBounds() == juce::Rectangle<int> (0, 46, 24, 46));

    Strip three ({ "a", "b", "c" }, 100);
    CHECK (three.tabs.getTab (0).getHeight() + three.tabs.getTab (1).getHeight()
           + three.tabs.getTab (2).getHeight() == 100);
    CHECK (three.tabs.getTab (2).getBottom() == 100);
}

TEST_CASE ("tabs: a click on a tab asks for it; on the lit one, nothing")
{
    Strip s;
    Pointer p;
    p.click (s.tabs.getTab (1), { 12, 20 });
    CHECK (s.asked == std::vector<int> { 1 });
    CHECK (s.tabs.getActive() == 1);
    p.click (s.tabs.getTab (1), { 12, 20 });
    CHECK (s.asked.size() == 1);
}

TEST_CASE ("tabs: the arrows move and choose, wrapping; Home and End go to the ends")
{
    Strip s ({ "a", "b", "c" }, 120);

    CHECK (key (s.tabs.getTab (0), juce::KeyPress::downKey));
    CHECK (s.tabs.getActive() == 1);
    CHECK (key (s.tabs.getTab (1), juce::KeyPress::rightKey));
    CHECK (s.tabs.getActive() == 2);
    CHECK (key (s.tabs.getTab (2), juce::KeyPress::downKey));   // wraps
    CHECK (s.tabs.getActive() == 0);
    CHECK (key (s.tabs.getTab (0), juce::KeyPress::upKey));     // and back
    CHECK (s.tabs.getActive() == 2);
    CHECK (key (s.tabs.getTab (2), juce::KeyPress::homeKey));
    CHECK (s.tabs.getActive() == 0);
    CHECK (key (s.tabs.getTab (0), juce::KeyPress::endKey));
    CHECK (s.asked == std::vector<int> { 1, 2, 0, 2, 0, 2 });

    /* Enter on the focused tab chooses it; Space is the host's. */
    CHECK_FALSE (ni::ui::test::windowUses (s.tabs.getTab (1), ni::ui::test::spaceKey));
    CHECK (s.tabs.getActive() == 2);
    CHECK (key (s.tabs.getTab (1), juce::KeyPress::returnKey));
    CHECK (s.tabs.getActive() == 1);
}

TEST_CASE ("tabs: one Tab stop, the lit tab")
{
    Strip s;
    CHECK (s.tabs.getTab (0).getWantsKeyboardFocus());
    CHECK_FALSE (s.tabs.getTab (1).getWantsKeyboardFocus());
    s.tabs.setActive (1);
    CHECK_FALSE (s.tabs.getTab (0).getWantsKeyboardFocus());
    CHECK (s.tabs.getTab (1).getWantsKeyboardFocus());
}

TEST_CASE ("tabs: an owner that does not light the tab leaves the strip as it was")
{
    Tabs t;
    t.setTabs ({ "Pattern", "Signal" });
    t.setBounds (0, 0, 24, 92);
    int asked = 0;
    t.onSelect = [&] (int) { ++asked; };
    Pointer p;
    p.click (t.getTab (1), { 12, 20 });
    CHECK (asked == 1);
    CHECK (t.getActive() == 0);
}

TEST_CASE ("tabs: a screen reader hears radio buttons, and which one is lit")
{
    Strip s;
    auto first = static_cast<juce::Component&> (s.tabs.getTab (0)).createAccessibilityHandler();
    auto second = static_cast<juce::Component&> (s.tabs.getTab (1)).createAccessibilityHandler();
    CHECK (first->getRole() == juce::AccessibilityRole::radioButton);
    CHECK (first->getTitle() == "Pattern");
    CHECK (first->getCurrentState().isChecked());
    CHECK_FALSE (second->getCurrentState().isChecked());

    CHECK (second->getActions().invoke (juce::AccessibilityActionType::press));
    CHECK (s.tabs.getActive() == 1);
    CHECK (second->getCurrentState().isChecked());
}

NI_SNAPSHOT_TEST ("tabs: strips lit, hovered, focused, and three to a strip")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-tabs")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-tabs");
}

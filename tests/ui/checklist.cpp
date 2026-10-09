// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The CheckList: a face that opens a panel of switches inside the window,
 * under the face and right-aligned to it; switches that ask for a selection
 * and show the one the owner then sets; refused rows shown, not hidden; the
 * keys; a press elsewhere closing it; and its states, as a picture.
 */
#include "CheckList.h"

#include "Gallery.h"
#include "Info.h"
#include "Pointer.h"
#include "Toggle.h"
#include "checks.h"
#include "hostkeys.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::CheckList;
using ni::ui::gallery::Pointer;
using ni::ui::gallery::key;

namespace
{
/* An editor's root: the window the panel opens in. */
struct Window final : public juce::Component, public ni::ui::InfoHost
{
    explicit Window (int w = 400, int h = 300) { setSize (w, h); }
    ni::ui::InfoState& infoState() override { return state; }
    ni::ui::InfoState state;
};

/* The Spectrogram's view: a labelled CheckList at (200, 20), whose owner
 * sets what it is asked for. */
struct Fixture
{
    explicit Fixture (int windowHeight = 300) : window (400, windowHeight)
    {
        list.setOptions ({ { 1, "Kick", "48k" }, { 2, "Bass", "48k" }, { 3, "Pad", "44.1k", true }, { 4, "Vocal", "48k" } });
        list.setSelected ({ 2 });
        list.setSummary ("Bass");
        list.setLabel ("View", 36);
        list.setFaceWidth (150);
        list.setBounds (200, 20, list.idealWidth(), 28);
        list.onChange = [this] (std::vector<int> ids)
        {
            asked.push_back (ids);
            list.setSelected (std::move (ids));
        };
        window.addAndMakeVisible (list);
    }

    juce::Component& face() { return *list.getChildComponent (0); }

    Window window;
    CheckList list;
    std::vector<std::vector<int>> asked;
};
} // namespace

TEST_CASE ("check list: the label, then the face space-2 after it, its own width")
{
    Fixture f;
    CHECK (f.list.face() == juce::Rectangle<int> (44, 0, 150, 28));
    CHECK (f.list.idealWidth() == 194);
    CHECK (f.face().getBounds() == f.list.face());
    CHECK (f.face().getTitle() == "Bass");
    CHECK (f.list.getTitle() == "View");
}

TEST_CASE ("check list: a click opens the panel in the window, under the face, right edges aligned")
{
    Fixture f;
    Pointer p;
    p.click (f.face(), { 20.0f, 14.0f });
    REQUIRE (f.list.isOpen());

    auto* panel = f.list.getPanel();
    REQUIRE (panel != nullptr);
    CHECK (panel->getParentComponent() == &f.window);
    const auto face = f.window.getLocalArea (&f.list, f.list.face());
    CHECK (panel->getRight() == face.getRight());
    CHECK (panel->getY() == face.getBottom() + 4);
    CHECK (panel->getWidth() >= 160);
    /* Four rows of 36 with 8 between, in 8 of padding and the hairline. */
    CHECK (panel->getHeight() == 4 * 36 + 3 * 8 + 2 * 8 + 2);

    p.click (f.face(), { 20.0f, 14.0f });
    CHECK_FALSE (f.list.isOpen());
    CHECK (f.window.getNumChildComponents() == 1);
}

TEST_CASE ("check list: a switch asks for the selection, added at the end or taken out")
{
    Fixture f;
    f.list.open();
    auto* kick = f.list.getSwitch (1);
    REQUIRE (kick != nullptr);
    CHECK_FALSE (kick->isOn());
    CHECK (f.list.getSwitch (2)->isOn());

    Pointer p;
    p.click (*kick, { 7.0f, 14.0f });
    REQUIRE (f.asked.size() == 1);
    CHECK (f.asked[0] == std::vector<int> { 2, 1 });
    CHECK (kick->isOn());              // shown from what the owner set
    CHECK (f.list.isOpen());           // choosing several: it stays open

    p.click (*f.list.getSwitch (2), { 7.0f, 14.0f });
    CHECK (f.asked[1] == std::vector<int> { 1 });
    CHECK_FALSE (f.list.getSwitch (2)->isOn());

    /* An owner that refuses: the switch shows what the owner holds. */
    f.list.onChange = [&] (std::vector<int> ids) { f.asked.push_back (ids); };
    p.click (*f.list.getSwitch (4), { 7.0f, 14.0f });
    CHECK_FALSE (f.list.getSwitch (4)->isOn());
}

TEST_CASE ("check list: a row at another rate is shown and refused")
{
    Fixture f;
    f.list.open();
    auto* pad = f.list.getSwitch (3);
    REQUIRE (pad != nullptr);
    CHECK_FALSE (pad->isEnabled());
    Pointer p;
    p.click (*pad, { 7.0f, 14.0f });
    CHECK (f.asked.empty());
}

TEST_CASE ("check list: a press elsewhere closes it; on the panel or the face it does not")
{
    Fixture f;
    f.list.open();
    f.list.pressedInWindow (f.list.getSwitch (1));
    CHECK (f.list.isOpen());
    f.list.pressedInWindow (&f.face());
    CHECK (f.list.isOpen());
    f.list.pressedInWindow (&f.window);
    CHECK_FALSE (f.list.isOpen());
}

TEST_CASE ("check list: Enter opens it, the arrows go through the switches past refused ones, Escape closes")
{
    Fixture f;
    CHECK_FALSE (ni::ui::test::windowUses (f.face(), ni::ui::test::spaceKey));
    CHECK_FALSE (f.list.isOpen());
    CHECK (key (f.face(), juce::KeyPress::returnKey));
    CHECK (f.list.isOpen());
    CHECK (key (f.face(), juce::KeyPress::escapeKey));
    CHECK_FALSE (f.list.isOpen());

    /* Down opens it and goes to the first switch; Up to the last. */
    CHECK (key (f.face(), juce::KeyPress::downKey));
    REQUIRE (f.list.isOpen());
    CHECK (f.list.focusSwitch (0, 1) == f.list.getSwitch (1));
    CHECK (f.list.focusSwitch (-1, -1) == f.list.getSwitch (4));

    /* From Bass down: Pad is refused, so Vocal; past the end, nothing. */
    CHECK (f.list.focusSwitch (f.list.indexOf (2) + 1, 1) == f.list.getSwitch (4));
    CHECK (f.list.focusSwitch (f.list.indexOf (4) + 1, 1) == nullptr);
    CHECK (f.list.focusSwitch (f.list.indexOf (4) - 1, -1) == f.list.getSwitch (2));

    /* A switch's own Up and Down reach its row, which moves on. */
    auto* bass = f.list.getSwitch (2);
    CHECK (key (*bass->getParentComponent(), juce::KeyPress::downKey));

    /* Escape on a switch closes the panel. */
    CHECK (key (*f.list.getPanel(), juce::KeyPress::escapeKey));
    CHECK_FALSE (f.list.isOpen());
}

TEST_CASE ("check list: a long list scrolls, and the keys keep their switch in sight")
{
    Fixture f (400);
    std::vector<CheckList::Option> many;
    for (int i = 1; i <= 12; ++i)
        many.push_back ({ i, "Bus " + juce::String (i), "48k" });
    f.list.setOptions (many);
    f.list.open();
    auto* panel = f.list.getPanel();
    REQUIRE (panel != nullptr);
    CHECK (panel->getHeight() == 220);

    auto* last = f.list.focusSwitch (-1, -1);
    REQUIRE (last != nullptr);
    const auto shown = panel->getLocalArea (last, last->getLocalBounds());
    CHECK (shown.getY() >= 0);
    CHECK (shown.getBottom() <= panel->getHeight());
}

TEST_CASE ("check list: with nothing to list it says so; disabled, nothing opens it")
{
    Fixture f;
    f.list.setOptions ({});
    f.list.setEmptyText ("no Listen-In found");
    f.list.open();
    REQUIRE (f.list.isOpen());
    CHECK (f.list.getSwitch (1) == nullptr);
    CHECK (f.list.focusSwitch (0, 1) == nullptr);
    f.list.close();

    f.list.setEnabled (false);
    f.list.open();
    CHECK_FALSE (f.list.isOpen());

    f.list.setEnabled (true);
    f.list.open();
    f.list.setEnabled (false);
    CHECK_FALSE (f.list.isOpen());
}

TEST_CASE ("check list: changing the options while open rebuilds the panel")
{
    Fixture f;
    f.list.open();
    f.list.setOptions ({ { 7, "Lead", "48k" } });
    CHECK (f.list.isOpen());
    CHECK (f.list.getSwitch (7) != nullptr);
    CHECK (f.list.getSwitch (1) == nullptr);
}

TEST_CASE ("check list: a screen reader hears a group named by its label, and a face named by its summary")
{
    Fixture f;
    auto group = static_cast<juce::Component&> (f.list).createAccessibilityHandler();
    REQUIRE (group != nullptr);
    CHECK (group->getRole() == juce::AccessibilityRole::group);
    auto face = f.face().createAccessibilityHandler();
    REQUIRE (face != nullptr);
    CHECK (face->getRole() == juce::AccessibilityRole::button);
    CHECK (face->getTitle() == "Bass");
    CHECK (face->getActions().invoke (juce::AccessibilityActionType::press));
    CHECK (f.list.isOpen());
}

NI_SNAPSHOT_TEST ("check list: its states, open with a refused row, and open with nothing")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-check-list")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-check-list");
}

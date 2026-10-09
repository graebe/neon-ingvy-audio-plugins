// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Space is the host's (Keys.h): in Live it starts and stops the transport,
 * and a plugin window that has the keyboard must let it through. So no
 * component of the kit uses Space or Shift+Space, in any state a gallery page
 * shows or a click leaves it in; only a text field being typed into takes it,
 * as a character. hostkeys.h walks a key through the window as JUCE's peer
 * does.
 */
#include "EditField.h"
#include "Gallery.h"
#include "Keys.h"
#include "Readout.h"
#include "TextField.h"
#include "hostkeys.h"

#include <doctest.h>

using namespace ni::ui;
using ni::ui::test::spaceKey;
using ni::ui::test::windowUses;

TEST_CASE ("space: the keyboard map has no Space; it is no key a control answers")
{
    CHECK (keys::keyOf (spaceKey) == keys::Key::none);
    CHECK (keys::keyOf (test::shiftSpaceKey) == keys::Key::none);
    CHECK (keys::padKey (spaceKey, 0, 16, 16).kind == keys::PadAction::Kind::none);
    CHECK (keys::padKey (test::shiftSpaceKey, 0, 16, 16).kind == keys::PadAction::Kind::none);
    CHECK (keys::sliderKey (spaceKey).kind == keys::SliderAction::Kind::none);
    CHECK_FALSE (keys::countKey (spaceKey, 16, 1, 128).has_value());
}

TEST_CASE ("space: no component on any gallery page uses Space, at rest or after a click")
{
    const auto pages = gallery::pages();
    REQUIRE_FALSE (pages.empty());
    for (const auto& page : pages)
    {
        CAPTURE (gallery::idOf (page));
        auto scene = page.make();
        REQUIRE (scene != nullptr);
        const auto users = test::spaceUsersAfterClicks (*scene);
        CHECK_MESSAGE (users.isEmpty(), users.joinIntoString ("\n").toStdString());
    }
}

TEST_CASE ("space: a text field typed into takes Space, as a character")
{
    SUBCASE ("a TextField: the name")
    {
        juce::Component window;
        TextField field;
        window.setBounds (0, 0, 200, 40);
        window.addAndMakeVisible (field);
        field.setBounds (0, 0, 160, 28);
        field.setValue ("Bass");
        field.moveCaretToEnd();

        CHECK (windowUses (field, spaceKey));
        CHECK (field.isBeingEdited());
        CHECK (field.getText() == "Bass ");
        /* The edit over, the field lets the keyboard go: the next Space is the
         * window's again, and so the host's. */
        field.finishEdit();
        CHECK_FALSE (field.isBeingEdited());
        CHECK (test::spaceUsers (window).isEmpty());
    }

    SUBCASE ("a Readout's field: a value typed in")
    {
        juce::Component window;
        Readout readout;
        window.setBounds (0, 0, 200, 40);
        window.addAndMakeVisible (readout);
        readout.setValueText ("40.0 ms");
        CHECK (test::spaceUsers (window).isEmpty());

        readout.showEditor();
        auto* field = readout.getCurrentTextEditor();
        REQUIRE (field != nullptr);
        field->moveCaretToEnd();
        CHECK (windowUses (*field, spaceKey));
        CHECK (field->getText() == "40.0 ms ");

        readout.hideEditor (false);   // Escape's, without waiting for its post
        CHECK (readout.getCurrentTextEditor() == nullptr);
        CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
        CHECK (test::spaceUsers (window).isEmpty());
    }

    SUBCASE ("an EditField: a pad's number")
    {
        juce::Component window;
        EditField field;
        window.setBounds (0, 0, 100, 40);
        window.addChildComponent (field);
        field.setBounds (0, 0, 64, 28);

        field.open ("3");
        field.moveCaretToEnd();
        CHECK (windowUses (field, spaceKey));
        CHECK (field.getText() == "3 ");
        field.abandon();
        CHECK_FALSE (field.isOpen());
    }
}

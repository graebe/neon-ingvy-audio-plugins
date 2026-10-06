// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The Readout: the plugin's text at rest, one click to type, Enter or a click
 * elsewhere to keep, Escape to abandon -- each edit ending exactly once, as
 * the web kit's lib/edit.js test holds its fields to -- and the typed text
 * handed to the plugin, never interpreted here.
 */
#include "Readout.h"

#include "Info.h"
#include "ParamBinding.h"
#include "UvTokens.h"
#include "events.h"
#include "fakes.h"
#include "pages.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::Readout;
using ni::ui::test::centreOf;
namespace c = uv::tok::colour;

namespace
{
/*
 * TextEditor posts Enter, Escape and a lost focus to itself as messages and
 * acts on them when the message loop delivers them -- as it does in a window.
 * The tests let it, for a moment.
 */
void settle()
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}

/* A readout that remembers what it was handed, and how often it closed. */
struct Typed
{
    Readout readout;
    std::vector<juce::String> commits;
    int closes = 0;

    Typed()
    {
        readout.setValueText ("32 ms");
        readout.onCommit = [this] (const juce::String& s) { commits.push_back (s); };
        readout.onClose = [this] { ++closes; };
    }

    juce::TextEditor& field()
    {
        auto* f = readout.getCurrentTextEditor();
        REQUIRE (f != nullptr);
        return *f;
    }

    void type (const juce::String& text) { field().setText (text, false); }

    void press (int key)
    {
        field().keyPressed (juce::KeyPress (key));
        settle();
    }
};
} // namespace

TEST_CASE ("readout: the card's size, and not a Tab stop")
{
    Readout r;
    CHECK (r.getHeight() == 28);
    CHECK (r.getWidth() == 64);
    CHECK_FALSE (r.getWantsKeyboardFocus());
}

TEST_CASE ("readout: the plugin's text, number in ink and unit in ink-muted")
{
    Readout r;
    r.setValueText ("32 ms");
    CHECK (r.getValueText() == "32 ms");

    const auto img = ni::ui::test::render (r);
    CHECK (img.getPixelAt (0, 14) == c::line200);   // the hairline
    CHECK (img.getPixelAt (4, 14) == c::bg000);     // the well

    /* Some pixel of the number reaches ink; the unit stays muted. */
    float number = 0.0f, unit = 0.0f;
    for (int y = 6; y < 22; ++y)
    {
        for (int x = 8; x < 32; ++x)
            number = juce::jmax (number, img.getPixelAt (x, y).getBrightness());
        for (int x = 36; x < 56; ++x)
            unit = juce::jmax (unit, img.getPixelAt (x, y).getBrightness());
    }
    CHECK (number > unit);
    CHECK (unit > 0.5f);

    /* Disabled: the hairline drops to line-100. */
    r.setEnabled (false);
    CHECK (ni::ui::test::render (r).getPixelAt (0, 14) == c::line100);
}

TEST_CASE ("readout: one click opens the field with the whole value selected")
{
    Typed t;
    ni::ui::test::click (t.readout, centreOf (t.readout));
    REQUIRE (t.readout.isBeingEdited());
    CHECK (t.field().getText() == "32 ms");
    CHECK (t.field().getHighlightedRegion() == juce::Range<int> (0, 5));
    CHECK (t.field().getJustificationType() == juce::Justification::centred);
    CHECK (t.readout.isCurrentlyModal());

    /* A drag across it is not a click. */
    Typed dragged;
    const auto at = centreOf (dragged.readout);
    dragged.readout.mouseUp (ni::ui::test::mouseAt (dragged.readout, at, {}, 1, at.translated (0.0f, -12.0f), true));
    CHECK_FALSE (dragged.readout.isBeingEdited());
}

TEST_CASE ("readout: Enter keeps what was typed, once, and the plugin's text comes back")
{
    Typed t;
    t.readout.showEditor();
    t.type ("40 ms");
    t.press (juce::KeyPress::returnKey);

    CHECK_FALSE (t.readout.isBeingEdited());
    CHECK_FALSE (t.readout.isCurrentlyModal());
    CHECK (t.commits == std::vector<juce::String> { "40 ms" });
    CHECK (t.closes == 1);
    /* Not the typing: the readout shows what the plugin says, and the plugin
     * has not said anything yet. */
    CHECK (t.readout.getValueText() == "32 ms");
}

TEST_CASE ("readout: Escape abandons the typing")
{
    Typed t;
    t.readout.showEditor();
    t.type ("40 ms");
    t.press (juce::KeyPress::escapeKey);

    CHECK_FALSE (t.readout.isBeingEdited());
    CHECK (t.commits.empty());
    CHECK (t.closes == 1);
}

TEST_CASE ("readout: a click elsewhere keeps what was typed, as Enter does")
{
    Typed t;
    t.readout.showEditor();
    t.type ("72 ms");
    /* What JUCE calls on the modal field's owner when a click lands outside. */
    t.readout.inputAttemptWhenModal();
    CHECK (t.commits == std::vector<juce::String> { "72 ms" });
    CHECK_FALSE (t.readout.isBeingEdited());
}

TEST_CASE ("readout: the focus moving on keeps the typing")
{
    Typed t;
    t.readout.showEditor();
    t.type ("1/8T");
    t.field().focusLost (juce::Component::focusChangedByTabKey);
    settle();
    CHECK (t.commits == std::vector<juce::String> { "1/8T" });
    CHECK_FALSE (t.readout.isBeingEdited());
}

TEST_CASE ("readout: an edit ends exactly once, however many endings arrive")
{
    /* lib/edit.js's bug: Escape closed the field, and the close's blur then
     * committed anyway. Here every ending after the first finds it closed. */
    Typed t;
    t.readout.showEditor();
    t.type ("40 ms");
    auto& field = t.field();
    field.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    field.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    settle();
    t.readout.inputAttemptWhenModal();
    t.readout.hideEditor (true);

    CHECK (t.commits.empty());
    CHECK (t.closes == 1);

    /* And Enter, then everything else: one commit. */
    Typed u;
    u.readout.showEditor();
    u.type ("40 ms");
    u.press (juce::KeyPress::returnKey);
    u.readout.inputAttemptWhenModal();
    u.readout.hideEditor (true);
    CHECK (u.commits == std::vector<juce::String> { "40 ms" });
    CHECK (u.closes == 1);
}

TEST_CASE ("readout: a value from the host never overwrites the typing, and shows after it")
{
    Typed t;
    t.readout.showEditor();
    t.type ("4");
    t.readout.setValueText ("50 ms");          // automation, while the field is open
    CHECK (t.field().getText() == "4");

    t.press (juce::KeyPress::escapeKey);
    CHECK (t.readout.getValueText() == "50 ms");
    CHECK (t.commits.empty());

    /* The next field opens on the value as it is now. */
    t.readout.showEditor();
    CHECK (t.field().getText() == "50 ms");
}

TEST_CASE ("readout: disabled, a click does nothing, and disabling abandons the field")
{
    Typed t;
    t.readout.setEnabled (false);
    ni::ui::test::click (t.readout, centreOf (t.readout));
    CHECK_FALSE (t.readout.isBeingEdited());

    t.readout.setEnabled (true);
    t.readout.showEditor();
    t.type ("40 ms");
    t.readout.setEnabled (false);
    CHECK_FALSE (t.readout.isBeingEdited());
    CHECK (t.commits.empty());
}

TEST_CASE ("readout: assistive technology reads the value and presses to type")
{
    Readout r;
    r.setValueText ("1/16");
    r.setTitle ("Rate value");

    /* getAccessibilityHandler() answers only on screen; this is what it makes. */
    const auto handler = r.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::button);
    CHECK (handler->getTitle() == "Rate value");
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->isReadOnly());
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "1/16");

    CHECK (handler->getActions().invoke (juce::AccessibilityActionType::press));
    CHECK (r.isBeingEdited());

    const auto line = juce::String::fromUTF8 ("Rate value \xe2\x80\x94 click to type a division, such as 1/16 or 1/8T.");
    ni::ui::setInfo (r, line);
    CHECK (r.getDescription() == line);
}

TEST_CASE ("readout: typed text reaches the plugin as one gesture, in the plugin's units")
{
    /* The binding a knob makes, here with only its readout: the plugin
     * formats and parses, the readout carries text both ways. */
    ni::ui::test::FakeParameters params;
    params.addFloat ("attack", "Attack", { 0.0f, 500.0f }, 32.0f,
                     [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; },
                     [] (const juce::String& s) { return s.getFloatValue(); });

    Readout r;
    ni::ui::ParamBinding binding (params[0], [&] { r.setValueText (binding.text()); });
    r.setValueText (binding.text());
    r.onCommit = [&] (const juce::String& typed) { binding.setText (typed); };
    CHECK (r.getValueText() == "32 ms");

    r.showEditor();
    r.getCurrentTextEditor()->setText ("40 ms", false);
    r.getCurrentTextEditor()->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    settle();

    CHECK (params.log() == "begin 0, value 0 0.080, end 0");
    CHECK (r.getValueText() == "40 ms");

    /* Enter on the value as it stands is kept too -- and the plugin, reading
     * the same value, writes nothing. */
    params.clear();
    r.showEditor();
    r.getCurrentTextEditor()->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
    settle();
    CHECK (params.log().isEmpty());
}

NI_SNAPSHOT_TEST ("readout: rest, unitless, editing, disabled, and the readout style")
{
    NI_CHECK_PAGE ("display-readout", "readout-states");
}

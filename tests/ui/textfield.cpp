// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * Typed text: an EditField's edit ends exactly once -- Enter or the focus
 * leaving keeps it, Escape abandons it, and the focus it gives up as it hides
 * is not a second end; a TextField commits once per edit, only a change,
 * puts the last value back on Escape and never holds more than the plugin
 * keeps. Then the states, as a picture.
 */
#include "EditField.h"
#include "Gallery.h"
#include "Pointer.h"
#include "TextField.h"
#include "checks.h"
#include "snapshot.h"

#include <doctest.h>

using ni::ui::EditField;
using ni::ui::TextField;
using ni::ui::gallery::key;

namespace
{
/* Types `text` into `e` as keys, one character each. */
void type (juce::TextEditor& e, const juce::String& text)
{
    for (auto ch : text)
        e.keyPressed (juce::KeyPress (0, {}, ch));
}

/* An EditField over a 64 x 28 readout, with what its owner heard. */
struct Edit
{
    Edit()
    {
        parent.setBounds (0, 0, 100, 40);
        parent.addChildComponent (field);
        field.setBounds (0, 0, 64, 28);
        field.onCommit = [this] (const juce::String& t) { commits.add (t); };
        field.onClose = [this] { ++closes; };
    }
    juce::Component parent;
    EditField field;
    juce::StringArray commits;
    int closes = 0;
};
} // namespace

/* ---------------------------------------------------------- edit field -- */

TEST_CASE ("edit field: it opens shown, with the whole value selected")
{
    Edit e;
    CHECK_FALSE (e.field.isVisible());
    e.field.open ("40.0 ms");
    CHECK (e.field.isOpen());
    CHECK (e.field.isVisible());
    CHECK (e.field.getText() == "40.0 ms");
    CHECK (e.field.getHighlightedRegion() == juce::Range<int> (0, 7));
}

TEST_CASE ("edit field: Enter keeps what was typed, once, and the field goes")
{
    Edit e;
    e.field.open ("40.0 ms");
    type (e.field, "25 ms");
    CHECK (key (e.field, juce::KeyPress::returnKey));

    CHECK (e.commits == juce::StringArray { "25 ms" });
    CHECK (e.closes == 1);
    CHECK_FALSE (e.field.isOpen());
    CHECK_FALSE (e.field.isVisible());

    /* The focus it gives up on the way, and a second Enter, end nothing. */
    e.field.focusLost (juce::Component::focusChangedDirectly);
    e.field.returnPressed();
    CHECK (e.commits.size() == 1);
    CHECK (e.closes == 1);
}

TEST_CASE ("edit field: Escape abandons -- no commit, even as the focus leaves after it")
{
    Edit e;
    e.field.open ("1/16");
    type (e.field, "1/8T");
    CHECK (key (e.field, juce::KeyPress::escapeKey));
    e.field.focusLost (juce::Component::focusChangedDirectly);

    CHECK (e.commits.isEmpty());
    CHECK (e.closes == 1);
    CHECK_FALSE (e.field.isVisible());
}

TEST_CASE ("edit field: a click elsewhere keeps what was typed")
{
    Edit e;
    e.field.open ("12");
    type (e.field, "3");
    e.field.focusLost (juce::Component::focusChangedByMouseClick);
    CHECK (e.commits == juce::StringArray { "3" });
    CHECK (e.closes == 1);
}

TEST_CASE ("edit field: an owner may delete it from onClose")
{
    juce::Component parent;
    auto field = std::make_unique<EditField>();
    parent.addChildComponent (*field);
    field->onClose = [&] { field.reset(); };
    field->open ("1");
    field->commit();
    CHECK (field == nullptr);
}

/* ---------------------------------------------------------- text field -- */

namespace
{
/* A TextField whose model is a string that keeps 8 characters, as the
 * Listen-In's keeps 31 bytes. */
struct Named
{
    Named()
    {
        field.setBounds (0, 0, 136, 28);
        field.setMaxLength (8);
        field.setValue ("Bus 1");
        field.onCommit = [this] (const juce::String& t)
        {
            commits.add (t);
            field.setValue (t);
        };
    }
    TextField field;
    juce::StringArray commits;
};
} // namespace

TEST_CASE ("text field: it shows the model's value until it is typed into")
{
    Named n;
    CHECK (n.field.getText() == "Bus 1");
    CHECK_FALSE (n.field.isBeingEdited());
    n.field.setValue ("Kick");
    CHECK (n.field.getText() == "Kick");
}

TEST_CASE ("text field: Enter commits once, and the focus leaving after it adds nothing")
{
    Named n;
    n.field.focusGained (juce::Component::focusChangedByMouseClick);
    CHECK (n.field.isBeingEdited());
    n.field.selectAll();
    type (n.field, "Bass");
    CHECK (n.commits.isEmpty());           // never per keystroke

    key (n.field, juce::KeyPress::returnKey);
    n.field.focusLost (juce::Component::focusChangedDirectly);
    CHECK (n.commits == juce::StringArray { "Bass" });
    CHECK_FALSE (n.field.isBeingEdited());
}

TEST_CASE ("text field: an edit that changes nothing commits nothing")
{
    Named n;
    n.field.focusGained (juce::Component::focusChangedByTabKey);
    n.field.focusLost (juce::Component::focusChangedByTabKey);
    CHECK (n.commits.isEmpty());
}

TEST_CASE ("text field: Escape puts the last value back and commits nothing")
{
    Named n;
    n.field.focusGained (juce::Component::focusChangedByMouseClick);
    n.field.selectAll();
    type (n.field, "Snare");
    key (n.field, juce::KeyPress::escapeKey);
    n.field.focusLost (juce::Component::focusChangedDirectly);

    CHECK (n.field.getText() == "Bus 1");
    CHECK (n.commits.isEmpty());
}

TEST_CASE ("text field: a click elsewhere keeps the edit; a model that changes it is shown")
{
    Named n;
    n.field.focusGained (juce::Component::focusChangedByMouseClick);
    n.field.selectAll();
    type (n.field, "Pads");
    n.field.focusLost (juce::Component::focusChangedByMouseClick);
    CHECK (n.commits == juce::StringArray { "Pads" });

    n.field.setValue ("Pads 2");   // the plugin's answer
    CHECK (n.field.getText() == "Pads 2");
}

TEST_CASE ("text field: it holds no more than the plugin keeps")
{
    Named n;
    n.field.focusGained (juce::Component::focusChangedByMouseClick);
    n.field.selectAll();
    type (n.field, "Kick and snare");
    CHECK (n.field.getText() == "Kick and");
}

TEST_CASE ("text field: disabled, no edit begins")
{
    Named n;
    n.field.setEnabled (false);
    n.field.focusGained (juce::Component::focusChangedByMouseClick);
    CHECK_FALSE (n.field.isBeingEdited());
}

NI_SNAPSHOT_TEST ("text field: its states, and an edit field open")
{
    ni::ui::gallery::Frame frame (ni::ui::gallery::pages());
    REQUIRE (frame.show (juce::String ("controls-text-field")));
    REQUIRE (frame.page() != nullptr);
    NI_CHECK_INFO_LIMIT (*frame.page());
    NI_CHECK_SNAPSHOT (*frame.page(), "controls-text-field");
}

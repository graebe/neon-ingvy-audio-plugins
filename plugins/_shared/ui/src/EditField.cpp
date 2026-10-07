// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A value edited in place. EditField.h has the rule that each edit ends once.
 */
#include "EditField.h"

#include "ChildLights.h"
#include "UvLight.h"
#include "UvType.h"

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;
} // namespace

EditField::EditField (const uv::tok::TextStyle& style)
{
    setMultiLine (false);
    setReturnKeyStartsNewLine (false);
    setPopupMenuEnabled (false);
    setScrollbarsShown (false);
    setEscapeAndReturnKeysConsumed (true);

    /* No padding: the box is the value's (.pad-order-edit { padding: 0 }),
     * inside its 1px hairline. */
    setBorder (juce::BorderSize<int> ((int) uv::tok::size::hairline));
    setIndents (0, 0);
    setJustification (juce::Justification::centred);

    setColour (juce::TextEditor::backgroundColourId, c::bg000);
    setColour (juce::TextEditor::textColourId, c::ink);
    setFont (uv::type::font (style));

    setVisible (false);
}

EditField::~EditField() = default;

void EditField::open (const juce::String& text)
{
    editing = true;
    setText (text, false);
    applyFontToAllText (getFont());
    applyColourToAllText (c::ink);
    setVisible (true);

    /* "Focused and selected the moment it appears." A field that is not on a
     * screen has no keyboard to take; it is still open, and still ends as
     * any other edit does. */
    if (isShowing())
        grabKeyboardFocus();
    selectAll();
    relight (*this);
}

void EditField::commit()  { finish (true); }
void EditField::abandon() { finish (false); }

void EditField::finish (bool keep)
{
    if (! editing)
        return;   // ended already: whatever ends it again is an echo

    editing = false;
    const auto typed = getText();
    relight (*this);

    juce::Component::SafePointer<EditField> self (this);
    if (keep && onCommit)
        onCommit (typed);
    if (self != nullptr && onClose)
        onClose();

    /* Gone when its edit is, as the web field is unmounted; the focus it
     * gives up on the way ends nothing, the edit having ended. */
    if (self != nullptr)
        setVisible (false);
}

void EditField::returnPressed()
{
    commit();
}

void EditField::escapePressed()
{
    abandon();
}

void EditField::focusLost (FocusChangeType cause)
{
    juce::TextEditor::focusLost (cause);
    /* A click elsewhere keeps what was typed. */
    commit();
}

void EditField::paintOverChildren (juce::Graphics& g)
{
    /* The uv hairline while open, whether or not this window has the
     * keyboard right now: the field is typed into until the edit ends. */
    g.setColour (editing ? c::uv : c::line200);
    g.drawRect (getLocalBounds().toFloat(), uv::tok::stroke::strokeHair);
}

void EditField::paintLight (juce::Graphics& g)
{
    if (editing && isVisible())
        uv::light::glowFocus (g, getLocalBounds().toFloat());
}

} // namespace ni::ui

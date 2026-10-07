// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A name, typed. TextField.h has when it commits and what it shows.
 */
#include "TextField.h"

#include "ChildLights.h"
#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

namespace ni::ui
{

namespace
{
namespace c = uv::tok::colour;
constexpr int hair = (int) uv::tok::size::hairline;
} // namespace

TextField::TextField()
{
    setMultiLine (false);
    setReturnKeyStartsNewLine (false);
    setPopupMenuEnabled (true);
    setScrollbarsShown (false);
    setEscapeAndReturnKeysConsumed (true);

    /* .ph-field { padding: 0 8px; border: 1px }: the text 9px in, centred on
     * the 28px line. */
    setBorder (juce::BorderSize<int> (hair));
    setIndents ((int) uv::tok::space::space2, 0);
    setJustification (juce::Justification::centredLeft);

    setColour (juce::TextEditor::backgroundColourId, c::bg200);
    setColour (juce::TextEditor::textColourId, c::ink);
    setFont (uv::type::value());
}

TextField::~TextField() = default;

void TextField::setValue (const juce::String& v)
{
    value = v;
    if (! editing)
    {
        setText (v, false);
        applyColourToAllText (isEnabled() ? c::ink : c::inkDim);
    }
}

void TextField::setPlaceholder (const juce::String& text)
{
    placeholder = text;
    repaint();
}

void TextField::setMaxLength (int n)
{
    setInputRestrictions (juce::jmax (0, n));
}

/* ============================================================== edit == */

void TextField::begin()
{
    if (editing || ! isEnabled())
        return;
    editing = true;
    focus.keyUsed();
    restyle();
    relight (*this);
}

void TextField::keep()
{
    if (! editing)
        return;
    editing = false;
    restyle();
    relight (*this);

    const auto typed = getText();
    if (typed != value && onCommit)
        onCommit (typed);   // may delete this
}

void TextField::cancel()
{
    if (! editing)
        return;
    editing = false;
    setText (value, false);
    restyle();
    relight (*this);
}

void TextField::leave()
{
    /* Enter and Escape end the edit and let the keyboard go, as a browser
     * field blurs: the next keys are the window's again. */
    if (hasKeyboardFocus (false))
        giveAwayKeyboardFocus();
}

void TextField::focusGained (FocusChangeType cause)
{
    juce::TextEditor::focusGained (cause);
    if (cause == focusChangedByTabKey)
        selectAll();
    focus.focusGained (cause);
    begin();
}

void TextField::focusLost (FocusChangeType cause)
{
    juce::TextEditor::focusLost (cause);
    focus.focusLost();
    keep();   // a click elsewhere keeps it
}

void TextField::finishEdit()
{
    juce::Component::SafePointer<TextField> self (this);
    keep();
    if (self != nullptr)
        leave();
}

void TextField::returnPressed()
{
    finishEdit();
}

void TextField::escapePressed()
{
    cancel();
    leave();
}

bool TextField::keyPressed (const juce::KeyPress& key)
{
    /* Typing is an edit, however the field came to have the keyboard. */
    begin();
    return juce::TextEditor::keyPressed (key);
}

void TextField::enablementChanged()
{
    if (! isEnabled())
        cancel();
    applyColourToAllText (isEnabled() ? c::ink : c::inkDim);
    restyle();
    juce::TextEditor::enablementChanged();
}

/* ============================================================ pointer == */

void TextField::mouseEnter (const juce::MouseEvent& e)
{
    juce::TextEditor::mouseEnter (e);
    hovered = true;
    restyle();
}

void TextField::mouseExit (const juce::MouseEvent& e)
{
    juce::TextEditor::mouseExit (e);
    hovered = false;
    restyle();
}

/* The well under the pointer rises to bg-300, until it is typed into. */
void TextField::restyle()
{
    setColour (juce::TextEditor::backgroundColourId, hovered && ! editing && isEnabled() ? c::bg300 : c::bg200);
    repaint();
}

/* ============================================================= paint == */

void TextField::paintOverChildren (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    /* The placeholder while it is empty, focused or not. */
    if (placeholder.isNotEmpty() && getTotalNumChars() == 0)
        uv::type::draw (g, placeholder,
                        r.reduced ((float) hair).withTrimmedLeft ((float) getLeftIndent()),
                        getFont(), isEnabled() ? c::inkMuted : c::inkDim);

    g.setColour (! isEnabled() ? c::line100 : editing ? c::uv : hovered ? c::inkDim : c::line200);
    g.drawRect (r, uv::tok::stroke::strokeHair);
}

void TextField::paintLight (juce::Graphics& g)
{
    if (editing)
        uv::light::glowFocus (g, getLocalBounds().toFloat());
}

} // namespace ni::ui

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The readout and its field. Readout.h has the card and the rules.
 */
#include "Readout.h"

#include "UvLight.h"
#include "UvTokens.h"
#include "UvType.h"

namespace ni::ui
{

namespace c = uv::tok::colour;

namespace
{
/* What a screen reader reads: the plugin's text, which it cannot set -- it
 * presses the readout to type, as a sighted user clicks it. */
class ReadoutValue final : public juce::AccessibilityTextValueInterface
{
public:
    explicit ReadoutValue (const Readout& r) : readout (r) {}

    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return readout.getValueText(); }
    void setValueAsString (const juce::String&) override {}

private:
    const Readout& readout;
};

class ReadoutAccessibility final : public juce::AccessibilityHandler
{
public:
    explicit ReadoutAccessibility (Readout& r)
        : juce::AccessibilityHandler (r, juce::AccessibilityRole::button,
                                      juce::AccessibilityActions().addAction (
                                          juce::AccessibilityActionType::press, [&r] { r.showEditor(); }),
                                      Interfaces { std::make_unique<ReadoutValue> (r) }),
          readout (r)
    {
    }

    /* While the field is open it is the field that has the focus and the
     * text; the readout steps aside, as juce::Label's handler does. */
    juce::AccessibleState getCurrentState() const override
    {
        if (readout.isBeingEdited())
            return {};
        return juce::AccessibilityHandler::getCurrentState();
    }

private:
    Readout& readout;
};
} // namespace

Readout::Readout()
{
    setWantsKeyboardFocus (false);
    setSize (minWidth, height);
}

Readout::~Readout()
{
    /* Destroyed while open: nothing is kept, and nobody is called back. */
    if (field != nullptr)
        field->removeListener (this);
}

void Readout::setValueText (const juce::String& text)
{
    if (text == valueText)
        return;

    /* While the field is open a value arriving from the host changes what the
     * readout will show after it, never what is being typed. */
    valueText = text;
    repaint();

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

/* ================================================================ field == */

void Readout::showEditor()
{
    if (field != nullptr || ! isEnabled())
        return;

    /* .readout.editing: the same box, the value centred in it, bg-000 under
     * ink. The uv ring is the readout's own (paintOverChildren), so it is
     * there whether or not the field has the focus yet. */
    field = std::make_unique<juce::TextEditor>();
    field->setBorder ({});
    field->setIndents (0, 0);
    field->setJustification (juce::Justification::centred);
    field->setColour (juce::TextEditor::backgroundColourId, c::bg000);
    field->setColour (juce::TextEditor::textColourId, c::ink);
    field->applyFontToAllText (uv::type::value());
    field->setText (valueText, false);
    field->addListener (this);
    addAndMakeVisible (*field);
    resized();

    /* "the box becomes a field with the value selected" */
    field->selectAll();

    /* Modal, so a click anywhere else ends the edit (inputAttemptWhenModal);
     * focused only where focus can be had -- on screen. */
    enterModalState (false);
    if (field->isShowing())
        field->grabKeyboardFocus();

    lightChanged (*this);
    repaint();
}

void Readout::hideEditor (bool keep)
{
    if (field == nullptr)
        return;

    /*
     * OUT OF ITS PLACE FIRST, then everything else. Whatever the closing sets
     * off -- the field losing the focus as it goes, a callback that closes the
     * readout again -- finds it already closed: the edit ends once. Destroying
     * the field from inside one of its own callbacks is safe (TextEditor
     * checks for it, as juce::Label relies on).
     */
    std::unique_ptr<juce::TextEditor> closing;
    std::swap (closing, field);
    const auto typed = closing->getText();
    closing->removeListener (this);
    closing.reset();

    exitModalState (0);
    lightChanged (*this);
    repaint();

    juce::Component::SafePointer<Readout> self (this);
    if (keep && onCommit)
        onCommit (typed);
    if (self != nullptr && onClose)
        onClose();
}

void Readout::textEditorReturnKeyPressed (juce::TextEditor&) { hideEditor (true); }
void Readout::textEditorEscapeKeyPressed (juce::TextEditor&) { hideEditor (false); }
void Readout::textEditorFocusLost (juce::TextEditor&)        { hideEditor (true); }

void Readout::inputAttemptWhenModal()
{
    hideEditor (true);
}

/* ============================================================== pointer == */

void Readout::mouseUp (const juce::MouseEvent& e)
{
    /* One click types: a release inside the box that did not drag. */
    if (getLocalBounds().toFloat().contains (e.position)
        && ! e.mouseWasDraggedSinceMouseDown() && ! e.mods.isPopupMenu())
        showEditor();
}

void Readout::enablementChanged()
{
    /* Disabled while typing: what was typed is abandoned, as Escape would. */
    if (! isEnabled())
        hideEditor (false);
    repaint();
}

/* ================================================================ paint == */

void Readout::resized()
{
    if (field != nullptr)
        field->setBounds (getLocalBounds());
}

void Readout::paintLight (juce::Graphics& g)
{
    if (isBeingEdited())
        uv::light::glowFocus (g, getLocalBounds().toFloat());
}

void Readout::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool live = isEnabled();

    g.setColour (c::bg000);
    g.fillRect (r);
    g.setColour (live ? c::line200 : c::line100);
    g.drawRect (r, uv::tok::stroke::strokeHair);

    /* padding: 0 space-2. The field draws its own text while it is open. */
    if (! isBeingEdited())
        uv::type::drawValueWithUnit (g, valueText, r.reduced (uv::tok::space::space2, 0.0f), live);
}

void Readout::paintOverChildren (juce::Graphics& g)
{
    /* .readout.editing { border-color: uv } -- over the field's own outline. */
    if (isBeingEdited())
    {
        g.setColour (c::uv);
        g.drawRect (getLocalBounds().toFloat(), uv::tok::stroke::strokeHair);
    }
}

std::unique_ptr<juce::AccessibilityHandler> Readout::createAccessibilityHandler()
{
    return std::make_unique<ReadoutAccessibility> (*this);
}

} // namespace ni::ui

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * The number under a control, and the field it becomes when typed into -- the
 * Readout card of Ultraviolet 1.1.0, and the web kit's .readout with its
 * EditField.
 *
 * AT REST: a 28px bg-000 box on a line-200 hairline, the value in `value`
 * type centred, its unit in ink-muted 2px after it (uv::type::drawValueWithUnit
 * splits them at the last space). Disabled, everything drops to ink-dim on a
 * line-100 hairline.
 *
 * TYPED INTO: one click (or showEditor(), which is Enter on the focused knob)
 * turns it into a field with the whole value selected, its border uv and
 * glow-focus around it. Enter, or a click anywhere else, keeps what was typed;
 * Escape abandons it. EACH EDIT ENDS EXACTLY ONCE -- the web kit's lib/edit.js
 * rule, written after Escape was found committing on its way out (the field's
 * blur did it) and Enter committing twice: whatever arrives after the first
 * ending is ignored.
 *
 * A CLICK ELSEWHERE IS A CLICK ELSEWHERE, whatever it lands on. The open field
 * is modal, as juce::Label's is: a click on a panel, which takes no focus,
 * still ends the edit, where waiting for the focus to move would leave the
 * field open under a pointer that has gone. That click does nothing else.
 *
 * THE READOUT NEVER FORMATS AND NEVER PARSES. It shows the plugin's text
 * (setValueText: "40.0 ms", "1/16", "90 %") and hands what was typed to
 * onCommit, for the plugin to read in its own units ("40 ms", "1/8T") --
 * ParamBinding::setText does exactly that, and writes nothing when the text
 * means the value it already has. After an edit the readout goes straight back
 * to the plugin's text: a value the plugin accepted arrives a moment later as
 * a new text, and a typo it refused leaves the old one, never the typo.
 *
 * NOT A TAB STOP. The control above it is: a knob takes the keyboard and opens
 * its readout with Enter (the Interaction conventions), so Tab never stops on
 * the number under it as well. onClose lets that control take the focus back.
 * Its info line is set like any other (ni::ui::setInfo), and says what it
 * accepts ("Rate value — click to type a division, such as 1/16 or 1/8T.").
 * To assistive technology it is what the web made it: a button that reads its
 * value and, pressed, opens the field.
 *
 * The 28px readout is not the big `readout` text style: that is the one
 * number a window is about, 28px type with no box, drawn with
 * uv::type::readout() where the window puts it.
 */
#pragma once

#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace ni::ui
{

class Readout : public juce::Component, public Luminous, private juce::TextEditor::Listener
{
public:
    Readout();
    ~Readout() override;

    /* The card's size: control-h tall, at least 64 wide (--readout-w). */
    static constexpr int height = 28;
    static constexpr int minWidth = 64;

    /* The plugin's text for the value now. */
    void setValueText (const juce::String&);
    const juce::String& getValueText() const noexcept { return valueText; }

    /* What was typed, when an edit is kept: Enter, a click elsewhere, the
     * focus moving on. Never Escape. */
    std::function<void (const juce::String& typed)> onCommit;
    /* After the field has closed, either way. */
    std::function<void()> onClose;

    /* Opens the field, the value selected; nothing if it is open or the
     * readout is disabled. */
    void showEditor();
    /* Closes it, keeping what was typed or not. Nothing if it is not open. */
    void hideEditor (bool keep);

    bool isBeingEdited() const noexcept { return field != nullptr; }
    juce::TextEditor* getCurrentTextEditor() const noexcept { return field.get(); }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void enablementChanged() override;

    /* A click outside the open field: it keeps what was typed. */
    void inputAttemptWhenModal() override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorFocusLost (juce::TextEditor&) override;

    juce::String valueText;
    std::unique_ptr<juce::TextEditor> field;

    JUCE_DECLARE_NON_COPYABLE (Readout)
};

} // namespace ni::ui

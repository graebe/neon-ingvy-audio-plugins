// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A field that edits a value in place: open with the value selected, kept on
 * Enter or a click elsewhere, abandoned on Escape -- the web kit's EditField
 * and lib/edit.js, natively.
 *
 * WHERE IT IS USED: over something that shows a value and is typed into
 * where it is -- the Trance Gate's arrival number on a pad, 20px of hint type.
 * (A knob's Readout is the same idea and keeps it itself: Readout.h.)
 *
 * EACH EDIT ENDS EXACTLY ONCE. The web kit's knob and pads once committed on
 * Escape -- removing a focused field fires its blur, whose handler committed --
 * and could commit twice on Enter. Here an edit is open() .. its end, and the
 * first of Enter, Escape or the focus leaving ends it: onCommit (Enter, focus
 * lost) then onClose, or onClose alone (Escape). What arrives after the end is
 * ignored -- the focus leaving as the field hides itself, above all.
 * Every one of them is handled at once, not as JUCE's TextEditor posts its own
 * (onReturnKey and the rest arrive a message later), so the owner can hide or
 * move the field from onClose.
 *
 * WHILE OPEN it looks typed into whatever has the focus: bg-000, a uv
 * hairline, glow-focus round it (the Readout card's editing state, and
 * .pad-order-edit). The value is the caller's to parse: this hands over what
 * was typed and never reads it.
 */
#pragma once

#include "Luminous.h"
#include "UvTokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace ni::ui
{

class EditField : public juce::TextEditor,
                  public Luminous
{
public:
    /* In `style`: `value` (13px) for a value, `hint` (10px) for a pad's
     * number. Centred, single line. */
    explicit EditField (const uv::tok::TextStyle& style = uv::tok::type::value);
    ~EditField() override;

    /* Starts an edit of `text`: shows the field, all of it selected, and
     * takes the keyboard (when it is on screen to take it). */
    void open (const juce::String& text);
    bool isOpen() const noexcept { return editing; }

    /* Ends the edit as Enter would, or as Escape would. */
    void commit();
    void abandon();

    std::function<void (const juce::String& typed)> onCommit;
    std::function<void()> onClose;

    /* ---- juce::TextEditor */
    void returnPressed() override;
    void escapePressed() override;
    void focusLost (FocusChangeType) override;
    void paintOverChildren (juce::Graphics&) override;

    /* ---- Luminous: glow-focus round it while open */
    void paintLight (juce::Graphics&) override;

private:
    void finish (bool keep);

    bool editing = false;

    JUCE_DECLARE_NON_COPYABLE (EditField)
};

} // namespace ni::ui

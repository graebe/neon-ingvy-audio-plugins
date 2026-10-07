// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A field for a name typed in: the TextField card of Ultraviolet 1.1.0, and the
 * Listen-In's name field that it was drawn from.
 *
 * A 28px (control-h) bg-200 well on a line-200 hairline, `value` text in ink
 * padded space-2, and while it is empty what belongs there in ink-muted ("name
 * this bus") -- shown until something is typed, focused or not, as a browser
 * shows a placeholder. Under the pointer the well rises to bg-300 and the
 * hairline is ink-dim (1.1.0's States); while it is typed into, its hairline
 * is uv with glow-focus round it; disabled, bg-100 on line-100 with ink-dim
 * text.
 *
 * ITS FOCUS IS ALWAYS VISIBLE WHILE IT IS TYPED INTO, however it was focused:
 * a browser's :focus-visible matches a text input focused by the pointer too,
 * as the caret shows. So the hint bar shows its line for as long as the edit
 * lasts (Focus.h). Tab into it selects what is there, so typing replaces it,
 * as a browser's input does; a click places the caret where it lands.
 *
 * ONE COMMIT PER EDIT, NEVER PER KEYSTROKE. An edit starts when the field takes
 * the keyboard and ends on the first of Enter (keep), a click elsewhere (keep)
 * or Escape (cancel, the last value back). A kept edit calls onCommit once, and
 * only if the text is not the value already: the Listen-In writes a name into
 * memory other plugins read, and a write per character would have every
 * reader's list flicker through "B", "Ba", "Bas" while it is typed.
 *
 * THE VALUE IS THE MODEL'S. setValue() is what the plugin holds; the field
 * shows it whenever it is not being typed into. After a commit it keeps what
 * was typed until the model says otherwise -- and a model that keeps less (the
 * plugin's 31 bytes) says so with setValue. It never shows more than the plugin
 * keeps: setMaxLength() is the plugin's limit, so a paste is cut where the
 * plugin would cut it.
 */
#pragma once

#include "Focus.h"
#include "Luminous.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace ni::ui
{

class TextField : public juce::TextEditor,
                  public Luminous
{
public:
    TextField();
    ~TextField() override;

    /* What the model holds. Shown whenever no edit is open. */
    void setValue (const juce::String&);
    const juce::String& getValue() const noexcept { return value; }

    /* What belongs here, in ink-muted while it is empty. */
    void setPlaceholder (const juce::String&);

    /* The most characters it takes: the plugin's limit (0: none). */
    void setMaxLength (int);

    /* An edit kept with a text that is not the value. Once per edit. */
    std::function<void (const juce::String&)> onCommit;

    bool isBeingEdited() const noexcept { return editing; }
    /* Ends an open edit as Enter does: kept, and the keyboard let go. What a
     * click elsewhere in the window does (EditorFrame). */
    void finishEdit();
    bool isHovered() const noexcept { return hovered; }

    /* ---- juce::TextEditor */
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    void returnPressed() override;
    void escapePressed() override;
    bool keyPressed (const juce::KeyPress&) override;
    void enablementChanged() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void paintOverChildren (juce::Graphics&) override;

    /* ---- Luminous: glow-focus round it while it is typed into */
    void paintLight (juce::Graphics&) override;

private:
    void begin();
    void keep();
    void cancel();
    void leave();
    void restyle();

    juce::String value, placeholder;
    bool editing = false;
    bool hovered = false;
    FocusVisibility focus { *this };

    JUCE_DECLARE_NON_COPYABLE (TextField)
};

} // namespace ni::ui

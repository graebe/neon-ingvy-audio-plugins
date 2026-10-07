// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A button, and buttons in a group: the Button and Actions cards, and the web
 * kit's Button.jsx.
 *
 * ONE WELL, TWO SHAPES. A word, optionally led by a 16px Icon at an 8px gap,
 * or the 28px square icon-only form for the transport and the window verbs.
 * 28px tall (control-h), padded space-3, the `button` text style:
 *
 *   rest       bg-200 well, line-200 hairline, ink
 *   hover      bg-300, its hairline ink-dim
 *   pressed    uv fill and hairline, on-uv text and glyph ("selected is uv")
 *   on         the same, latched -- and an icon-only button that is on is lit
 *              like a step, with glow-led (Button card)
 *   primary    always uv, with glow-led: at most once per window
 *   disabled   bg-100 on line-100, ink-dim, no hover
 *   focus      glow-focus round it, for the keyboard only
 *
 * The glyph is the design's own, in the colour of the text (UvIcons.h).
 *
 * AN ICON REPLACES A VERB, and only on a control that acts. An icon-only
 * button carries its verb as its accessible title (setTitle: "Copy slot"),
 * and like every button its info line, which is also its description: the
 * hint bar shows that line, so there is no tooltip besides.
 *
 * Buttons are for actions, never for toggling a parameter -- that is a
 * Toggle. `on` is for a button whose action is a mode (the Trance Gate's
 * ORDER) or a latching transport key (the Spectrogram's pause).
 */
#pragma once

#include "Pressable.h"

#include <functional>
#include <vector>

namespace ni::ui
{

class Button : public Pressable
{
public:
    explicit Button (const juce::String& text = {}, const juce::String& icon = {});
    ~Button() override;

    /* The word, and the glyph before it: one of the system's fifteen, or
     * none. With a glyph and no word, it is the 28px square. */
    void setText (const juce::String&);
    void setIcon (const juce::String&);
    const juce::String& getText() const noexcept { return text; }
    const juce::String& getIcon() const noexcept { return icon; }
    bool isIconOnly() const noexcept { return icon.isNotEmpty() && text.isEmpty(); }

    /* Lit and latched. A button given a state is a toggle to a screen
     * reader from then on (aria-pressed). */
    void setOn (bool);
    bool isOn() const noexcept { return on; }

    /* The window's one primary action. */
    void setPrimary (bool);
    bool isPrimary() const noexcept { return primary; }

    /* Where the content sits: centred, or from the left padding (the
     * labelled Actions stack). */
    void setContentLeft (bool);

    /* Its own width: the square, or the padding, the glyph, the gap and the
     * word. Height is always control-h. */
    int idealWidth() const;

    std::function<void()> onClick;

    /* Whether it is lit: on, primary or pressed, and enabled. */
    bool isLit() const;

    void paint (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;

protected:
    void pressed() override;
    std::optional<bool> checkedState() const override;

private:
    bool glows() const;

    juce::String text, icon;
    bool on = false, latching = false, primary = false, left = false;

    JUCE_DECLARE_NON_COPYABLE (Button)
};

/*
 * Buttons that belong together (Actions):
 *
 *   joined   edge to edge, sharing one hairline -- the window verbs as icons
 *            (copy, paste, export, export all, import: 136px for five) and the
 *            transport. The button under the pointer, the focused one and a
 *            lit one are raised over their neighbours, so the shared hairline
 *            is theirs and their light falls over the buttons beside them.
 *   column   joined top to bottom instead, each sharing its top hairline with
 *            the bottom one of the button above: the icon verbs in a side
 *            column too narrow for a row of them (the Trance Gate's six, 163px
 *            tall). The Actions card's "left to right or top to bottom".
 *   stack    one above the other, space-2 apart, all as wide as the widest,
 *            glyph then word from the left: the labelled window verbs.
 *
 * The group lays its buttons out from the top-left corner at their own sizes;
 * give it idealSize(). It does not own them.
 */
class ButtonGroup : public juce::Component,
                    public Luminous,
                    public PressableParent
{
public:
    enum class Form { joined, column, stack };

    explicit ButtonGroup (Form = Form::joined);
    ~ButtonGroup() override;

    /* Adds a button, after the others. */
    void add (Button&);
    int size() const noexcept { return (int) buttons.size(); }

    juce::Rectangle<int> idealSize() const;

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void paintLight (juce::Graphics&) override;
    void pressableStateChanged (Pressable&) override;

private:
    bool isJoined() const noexcept { return form != Form::stack; }
    bool raised (const Button&) const;
    void restack();
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    const Form form;
    std::vector<Button*> buttons;

    JUCE_DECLARE_NON_COPYABLE (ButtonGroup)
};

} // namespace ni::ui

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A hairlined bg-100 box that groups the controls of one function -- the Panel
 * card of Ultraviolet 1.1.0, and the web kit's .panel.
 *
 * ONE PANEL PER FUNCTION, never one per control, never nested, a title of one
 * or two words: the card's rules, which are the editor's to keep. What the
 * panel keeps is its look -- bg-100, a line-100 hairline, space-4 padding --
 * and where its controls go: contentBounds(). Where it sits and how big it is
 * belong to the editor that has one.
 *
 * TWO FORMS.
 *
 *   standard   the title above the controls, top left, in the `title` style
 *              (12px, uppercase, tracked, ink-muted), 12px over them
 *   compact    for a window with three or more panels: 140px tall -- exactly
 *              one knob card inside the padding and hairlines -- the title
 *              running up the left edge, read bottom to top, centred on the
 *              panel's height, space-3 before the controls. Every panel in
 *              such a window is compact, never a mix. (The web editors draw
 *              every panel this way.)
 *
 * THE TITLE CARRIES THE PANEL'S INFO LINE ("Gate — when the steps fall and
 * how much of each one sounds."), so pointing at it says what the group is
 * for: setTitleInfo() puts it on the title, which is a component of its own
 * for exactly that, and on the panel as its accessible description. To
 * assistive technology the panel is a group named by its title.
 *
 * A panel is solid to the Ground's rings (WaveSource.h), and it paints the
 * light of the controls inside it, as every kit container does (Luminous.h).
 */
#pragma once

#include "UvTokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace ni::ui
{

class Panel : public juce::Component
{
public:
    enum class Form
    {
        standard,
        compact,
    };

    explicit Panel (const juce::String& title = {}, Form = Form::standard);
    ~Panel() override;

    /* The hairline, then space-4 of padding, on every side. */
    static constexpr int inset = (int) uv::tok::size::hairline + (int) uv::tok::space::space4;
    /* "Height 140px, exactly one knob card": 1 + 16 + 106 + 16 + 1. */
    static constexpr int compactHeight = 140;
    /* The title's line, and the gap between it and the controls. */
    static constexpr int titleLine = 16;
    static constexpr int titleGap = 12;

    void setTitleText (const juce::String&);
    const juce::String& getTitleText() const noexcept { return titleText; }

    void setForm (Form);
    Form getForm() const noexcept { return form; }

    /* The panel's info line, on its title (and its accessible description). */
    void setTitleInfo (const juce::String& line);

    /* The title, as a component: where the pointer has to be for its line. */
    juce::Component& titleComponent() noexcept;

    /* Where the controls go: inside the hairline and the padding, past the
     * title. */
    juce::Rectangle<int> contentBounds() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class Title;

    juce::String titleText;
    Form form;
    std::unique_ptr<Title> title;

    JUCE_DECLARE_NON_COPYABLE (Panel)
};

} // namespace ni::ui
